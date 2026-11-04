// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Measure tools' Camera (AR) instrument (MeasureDevices::startAr,
// docs/MEASURE.md): a full-screen UIKit view controller over the Qt window
// with ARKit's camera view (ARSCNView, world tracking, planes found
// horizontally and vertically, scene depth where there is LiDAR), Apple's
// coaching overlay, a crosshair, and the points placed where the crosshair
// meets a surface (a raycast on estimated planes from the view's middle).
// Points, segments and their lengths are drawn live; Done hands back the
// distances, the outline on its surface, or the box's sizes, always in mm.
// Everything here runs on the main thread: a display link raycasts and
// moves the labels each frame (no SceneKit render-thread callbacks).
// Compiled for iOS only, with ARC (src/ui/CMakeLists.txt).

#include "ui/ios/MeasureIos.h"

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <simd/simd.h>

#import <ARKit/ARKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <SceneKit/SceneKit.h>
#import <UIKit/UIKit.h>

#if !__has_feature(objc_arc)
#error "ArMeasureIos.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

using os::ui::ArMode;
using os::ui::ArResult;

namespace {

// A line drawn between two points (meters, ARKit's world: y up), with its
// label (nil: none). Live: it ends at the crosshair, not at a placed point.
struct Segment {
    simd_float3 a;
    simd_float3 b;
    NSString* label;
    bool live;
};

NSString* lengthText(double meters, bool inches)
{
    if (inches)
        return [NSString stringWithFormat:@"%.2f in", meters / 0.0254];
    if (meters >= 1.0)
        return [NSString stringWithFormat:@"%.2f m", meters];
    return [NSString stringWithFormat:@"%.0f mm", meters * 1000.0];
}

double lengthBetween(simd_float3 a, simd_float3 b)
{
    return static_cast<double>(simd_length(b - a));
}

// A box from up to four points: three corners of its base (the base taken
// level, at their mean height: a box stands on a floor or a table), then a
// point on its top. Width: corner 1 to corner 2; depth: corner 3's distance
// from that side; height: the top point above the base.
struct Box {
    simd_float3 corner[4] = {}; // the base: 1, 2, 2 + depth, 1 + depth
    simd_float3 up = simd_make_float3(0, 0, 0); // from the base to the top
    double width = 0;
    double depth = 0;
    double height = 0;
    bool hasDepth = false;
    bool hasHeight = false;
};

Box boxFrom(const std::vector<simd_float3>& points)
{
    Box box;
    if (points.size() < 2)
        return box;
    const size_t baseCount = std::min<size_t>(points.size(), 3);
    float baseY = 0;
    for (size_t i = 0; i < baseCount; ++i)
        baseY += points[i].y;
    baseY /= static_cast<float>(baseCount);
    auto level = [baseY](simd_float3 p) { return simd_make_float3(p.x, baseY, p.z); };
    const simd_float3 a = level(points[0]);
    const simd_float3 side = level(points[1]) - a;
    const float width = simd_length(side);
    box.width = static_cast<double>(width);
    simd_float3 across = simd_make_float3(0, 0, 0);
    if (points.size() >= 3 && width > 1e-6f) {
        const simd_float3 along = side / width;
        const simd_float3 toThird = level(points[2]) - a;
        across = toThird - simd_dot(toThird, along) * along;
        box.depth = static_cast<double>(simd_length(across));
        box.hasDepth = true;
    }
    box.corner[0] = a;
    box.corner[1] = a + side;
    box.corner[2] = a + side + across;
    box.corner[3] = a + across;
    if (points.size() >= 4) {
        const float rise = points[3].y - baseY;
        box.height = std::fabs(static_cast<double>(rise));
        box.up = simd_make_float3(0, rise, 0);
        box.hasHeight = true;
    }
    return box;
}

// What to draw for the points placed and, when the crosshair is on a
// surface, the point it would place.
std::vector<Segment> segmentsFor(ArMode mode, const std::vector<simd_float3>& placed, bool hasHit, simd_float3 hit,
                                 bool inches)
{
    std::vector<Segment> segments;
    if (mode == ArMode::Box) {
        std::vector<simd_float3> points = placed;
        const bool live = hasHit && points.size() < 4;
        if (live)
            points.push_back(hit);
        if (points.size() < 2)
            return segments;
        const Box box = boxFrom(points);
        const bool liveWidth = live && points.size() == 2;
        segments.push_back({box.corner[0], box.corner[1],
                            [NSString stringWithFormat:@"W %@", lengthText(box.width, inches)], liveWidth});
        if (box.hasDepth) {
            const bool liveDepth = live && points.size() == 3;
            segments.push_back({box.corner[1], box.corner[2],
                                [NSString stringWithFormat:@"D %@", lengthText(box.depth, inches)], liveDepth});
            segments.push_back({box.corner[2], box.corner[3], nil, liveDepth});
            segments.push_back({box.corner[3], box.corner[0], nil, liveDepth});
        }
        if (box.hasHeight) {
            const bool liveHeight = live && points.size() == 4;
            for (int i = 0; i < 4; ++i) {
                NSString* label =
                    i == 1 ? [NSString stringWithFormat:@"H %@", lengthText(box.height, inches)] : nil;
                segments.push_back({box.corner[i], box.corner[i] + box.up, label, liveHeight});
                segments.push_back({box.corner[i] + box.up, box.corner[(i + 1) % 4] + box.up, nil, liveHeight});
            }
        }
        return segments;
    }

    for (size_t i = 1; i < placed.size(); ++i)
        segments.push_back({placed[i - 1], placed[i], lengthText(lengthBetween(placed[i - 1], placed[i]), inches), false});
    if (!placed.empty() && hasHit)
        segments.push_back({placed.back(), hit, lengthText(lengthBetween(placed.back(), hit), inches), true});
    if (mode == ArMode::Outline) {
        // The outline closes itself: from the crosshair (or the last point)
        // back to the first.
        if (hasHit && placed.size() >= 2)
            segments.push_back({hit, placed.front(), lengthText(lengthBetween(hit, placed.front()), inches), true});
        else if (!hasHit && placed.size() >= 3)
            segments.push_back(
                {placed.back(), placed.front(), lengthText(lengthBetween(placed.back(), placed.front()), inches), false});
    }
    return segments;
}

// The outline's points on their plane, in mm: the plane's normal from
// Newell's formula (a least-squares fit for a polygon), turned toward the
// camera; x along the first side, y = normal x x, so that seen from the
// camera's side x points right and y up, and a counter-clockwise outline
// stays counter-clockwise. False when the points span no plane.
bool outlineOnPlane(const std::vector<simd_float3>& points, simd_float3 cameraPosition, bool hasCamera,
                    std::vector<QPointF>& outline)
{
    const size_t n = points.size();
    if (n < 3)
        return false;
    simd_float3 normal = simd_make_float3(0, 0, 0);
    simd_float3 centroid = simd_make_float3(0, 0, 0);
    for (size_t i = 0; i < n; ++i) {
        const simd_float3 p = points[i];
        const simd_float3 q = points[(i + 1) % n];
        normal.x += (p.y - q.y) * (p.z + q.z);
        normal.y += (p.z - q.z) * (p.x + q.x);
        normal.z += (p.x - q.x) * (p.y + q.y);
        centroid += p;
    }
    centroid /= static_cast<float>(n);
    if (simd_length(normal) < 1e-9f)
        return false;
    normal = simd_normalize(normal);
    const simd_float3 towardViewer = hasCamera ? cameraPosition - centroid : simd_make_float3(0, 1, 0);
    if (simd_dot(normal, towardViewer) < 0)
        normal = -normal;
    simd_float3 xAxis = points[1] - points[0];
    xAxis -= simd_dot(xAxis, normal) * normal;
    if (simd_length(xAxis) < 1e-6f)
        return false;
    xAxis = simd_normalize(xAxis);
    const simd_float3 yAxis = simd_cross(normal, xAxis);
    outline.clear();
    for (const simd_float3& p : points) {
        const simd_float3 d = p - points[0];
        outline.emplace_back(static_cast<double>(simd_dot(d, xAxis)) * 1000.0,
                             static_cast<double>(simd_dot(d, yAxis)) * 1000.0);
    }
    return true;
}

SCNMaterial* flatMaterial(UIColor* color)
{
    SCNMaterial* material = [SCNMaterial material];
    material.diffuse.contents = color;
    material.lightingModelName = SCNLightingModelConstant;
    return material;
}

SCNNode* sphereNode(CGFloat radius, UIColor* color)
{
    SCNSphere* sphere = [SCNSphere sphereWithRadius:radius];
    sphere.materials = @[ flatMaterial(color) ];
    return [SCNNode nodeWithGeometry:sphere];
}

// A thin cylinder one meter long along y, scaled and turned to a segment.
SCNNode* segmentNode()
{
    SCNCylinder* cylinder = [SCNCylinder cylinderWithRadius:0.0015 height:1.0];
    cylinder.materials = @[ flatMaterial(UIColor.whiteColor) ];
    return [SCNNode nodeWithGeometry:cylinder];
}

void placeSegment(SCNNode* node, simd_float3 a, simd_float3 b)
{
    simd_float3 d = b - a;
    const float length = simd_length(d);
    if (length < 1e-5f) {
        node.hidden = YES;
        return;
    }
    node.hidden = NO;
    if (d.y < 0)
        d = -d; // the cylinder is symmetric: never turn it upside down
    node.simdPosition = (a + b) * 0.5f;
    node.simdScale = simd_make_float3(1, length, 1);
    node.simdOrientation = simd_quaternion(simd_make_float3(0, 1, 0), d / length);
}

void impact(UIImpactFeedbackStyle style)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIImpactFeedbackGenerator* generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
#pragma clang diagnostic pop
    [generator impactOccurred];
}

UIColor* panelColor()
{
    return [UIColor colorWithWhite:0.08 alpha:0.72];
}

UIColor* liveColor()
{
    return [UIColor colorWithRed:1.0 green:0.82 blue:0.25 alpha:1.0];
}

} // namespace

@interface OSArMeasureController : UIViewController <ARSCNViewDelegate>
- (instancetype)initWithMode:(ArMode)mode
                      inches:(BOOL)inches
                    finished:(const std::function<void(const ArResult&)>&)finished;
@end

@implementation OSArMeasureController {
    ArMode _mode;
    BOOL _inches;
    BOOL _lidar;
    BOOL _done;
    std::function<void(const ArResult&)> _finished;

    ARSCNView* _sceneView;
    ARCoachingOverlayView* _coaching;
    UIView* _crosshair;
    UILabel* _instruction;
    UILabel* _readout;
    UIView* _readoutPill;
    UIButton* _addButton;
    UIButton* _undoButton;
    UIButton* _doneButton;
    UIButton* _cancelButton;
    CADisplayLink* _displayLink;

    std::vector<simd_float3> _points;
    BOOL _hasHit;
    simd_float3 _hit;
    SCNNode* _hitNode;
    NSMutableArray<SCNNode*>* _pointNodes;
    NSMutableArray<SCNNode*>* _segmentNodes; // a pool, reused each frame
    NSMutableArray<UILabel*>* _segmentLabels; // a pool, reused each frame
    BOOL _loggedFirstHit;
}

- (instancetype)initWithMode:(ArMode)mode
                      inches:(BOOL)inches
                    finished:(const std::function<void(const ArResult&)>&)finished
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _mode = mode;
        _inches = inches;
        _finished = finished;
        _done = NO;
        _hasHit = NO;
        _hit = simd_make_float3(0, 0, 0);
        _loggedFirstHit = NO;
        _lidar = [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth];
        _pointNodes = [[NSMutableArray alloc] init];
        _segmentNodes = [[NSMutableArray alloc] init];
        _segmentLabels = [[NSMutableArray alloc] init];
    }
    return self;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

// ---- Layout

- (UIView*)pillAround:(UILabel*)label
{
    UIView* pill = [[UIView alloc] initWithFrame:CGRectZero];
    pill.translatesAutoresizingMaskIntoConstraints = NO;
    pill.backgroundColor = panelColor();
    pill.layer.cornerRadius = 14;
    pill.userInteractionEnabled = NO;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [pill addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
        [label.leadingAnchor constraintEqualToAnchor:pill.leadingAnchor constant:14],
        [label.trailingAnchor constraintEqualToAnchor:pill.trailingAnchor constant:-14],
        [label.topAnchor constraintEqualToAnchor:pill.topAnchor constant:8],
        [label.bottomAnchor constraintEqualToAnchor:pill.bottomAnchor constant:-8],
    ]];
    return pill;
}

- (UIButton*)buttonTitled:(NSString*)title symbol:(NSString*)symbol action:(SEL)action
{
    UIButtonConfiguration* configuration = [UIButtonConfiguration filledButtonConfiguration];
    configuration.title = title;
    configuration.image = [UIImage systemImageNamed:symbol];
    configuration.imagePadding = 6;
    configuration.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    configuration.baseBackgroundColor = panelColor();
    configuration.baseForegroundColor = UIColor.whiteColor;
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(12, 18, 12, 18);
    UIButton* button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:button];
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:50].active = YES;
    return button;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    _sceneView = [[ARSCNView alloc] initWithFrame:self.view.bounds];
    _sceneView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _sceneView.scene = [SCNScene scene];
    _sceneView.delegate = self;
    _sceneView.automaticallyUpdatesLighting = NO; // flat colors only
    [self.view addSubview:_sceneView];

    _hitNode = sphereNode(0.004, liveColor());
    _hitNode.hidden = YES;
    [_sceneView.scene.rootNode addChildNode:_hitNode];

    // The crosshair: a ring with a dot, in the middle of the view (where
    // the raycast starts).
    _crosshair = [[UIView alloc] initWithFrame:CGRectZero];
    _crosshair.translatesAutoresizingMaskIntoConstraints = NO;
    _crosshair.userInteractionEnabled = NO;
    _crosshair.layer.borderColor = UIColor.whiteColor.CGColor;
    _crosshair.layer.borderWidth = 2;
    _crosshair.layer.cornerRadius = 18;
    UIView* dot = [[UIView alloc] initWithFrame:CGRectZero];
    dot.translatesAutoresizingMaskIntoConstraints = NO;
    dot.backgroundColor = UIColor.whiteColor;
    dot.layer.cornerRadius = 3;
    [_crosshair addSubview:dot];
    [self.view addSubview:_crosshair];

    _readout = [[UILabel alloc] initWithFrame:CGRectZero];
    _readout.font = [UIFont monospacedDigitSystemFontOfSize:26 weight:UIFontWeightSemibold];
    _readout.textColor = UIColor.whiteColor;
    _readout.textAlignment = NSTextAlignmentCenter;
    _readoutPill = [self pillAround:_readout];
    [self.view addSubview:_readoutPill];

    // Apple's coaching overlay: shown while ARKit looks for a surface.
    _coaching = [[ARCoachingOverlayView alloc] initWithFrame:CGRectZero];
    _coaching.translatesAutoresizingMaskIntoConstraints = NO;
    _coaching.session = _sceneView.session;
    _coaching.goal = ARCoachingGoalAnyPlane;
    _coaching.activatesAutomatically = YES;
    [self.view addSubview:_coaching];

    _instruction = [[UILabel alloc] initWithFrame:CGRectZero];
    _instruction.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
    _instruction.textColor = UIColor.whiteColor;
    _instruction.textAlignment = NSTextAlignmentCenter;
    _instruction.numberOfLines = 0;
    UIView* instructionPill = [self pillAround:_instruction];
    [self.view addSubview:instructionPill];

    _cancelButton = [self buttonTitled:@"Cancel" symbol:@"xmark" action:@selector(cancelTapped)];
    _undoButton = [self buttonTitled:@"Undo" symbol:@"arrow.uturn.backward" action:@selector(undoTapped)];
    _doneButton = [self buttonTitled:@"Done" symbol:@"checkmark" action:@selector(doneTapped)];

    // The big round Add point button.
    UIButtonConfiguration* addConfiguration = [UIButtonConfiguration filledButtonConfiguration];
    addConfiguration.image = [UIImage systemImageNamed:@"plus"];
    addConfiguration.preferredSymbolConfigurationForImage =
        [UIImageSymbolConfiguration configurationWithPointSize:30 weight:UIImageSymbolWeightBold];
    addConfiguration.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    addConfiguration.baseBackgroundColor = UIColor.whiteColor;
    addConfiguration.baseForegroundColor = UIColor.blackColor;
    _addButton = [UIButton buttonWithConfiguration:addConfiguration primaryAction:nil];
    _addButton.translatesAutoresizingMaskIntoConstraints = NO;
    _addButton.accessibilityLabel = @"Add point";
    [_addButton addTarget:self action:@selector(addTapped) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_addButton];
    UILabel* addCaption = [[UILabel alloc] initWithFrame:CGRectZero];
    addCaption.translatesAutoresizingMaskIntoConstraints = NO;
    addCaption.text = @"Add point";
    addCaption.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    addCaption.textColor = UIColor.whiteColor;
    addCaption.shadowColor = [UIColor colorWithWhite:0 alpha:0.6];
    addCaption.shadowOffset = CGSizeMake(0, 1);
    [self.view addSubview:addCaption];

    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_crosshair.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [_crosshair.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [_crosshair.widthAnchor constraintEqualToConstant:36],
        [_crosshair.heightAnchor constraintEqualToConstant:36],
        [dot.centerXAnchor constraintEqualToAnchor:_crosshair.centerXAnchor],
        [dot.centerYAnchor constraintEqualToAnchor:_crosshair.centerYAnchor],
        [dot.widthAnchor constraintEqualToConstant:6],
        [dot.heightAnchor constraintEqualToConstant:6],

        [_readoutPill.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [_readoutPill.topAnchor constraintEqualToAnchor:_crosshair.bottomAnchor constant:18],

        [_coaching.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_coaching.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_coaching.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_coaching.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        [_cancelButton.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [_cancelButton.topAnchor constraintEqualToAnchor:safe.topAnchor constant:10],

        [instructionPill.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [instructionPill.topAnchor constraintEqualToAnchor:_cancelButton.bottomAnchor constant:12],
        [instructionPill.widthAnchor constraintLessThanOrEqualToAnchor:safe.widthAnchor constant:-32],
        [instructionPill.widthAnchor constraintLessThanOrEqualToConstant:560],

        [_addButton.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_addButton.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-40],
        [_addButton.widthAnchor constraintEqualToConstant:80],
        [_addButton.heightAnchor constraintEqualToConstant:80],
        [addCaption.centerXAnchor constraintEqualToAnchor:_addButton.centerXAnchor],
        [addCaption.topAnchor constraintEqualToAnchor:_addButton.bottomAnchor constant:6],

        [_undoButton.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [_undoButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [_doneButton.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [_doneButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
    ]];

    [self refresh];
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    ARWorldTrackingConfiguration* configuration = [[ARWorldTrackingConfiguration alloc] init];
    configuration.planeDetection = ARPlaneDetectionHorizontal | ARPlaneDetectionVertical;
    if (_lidar)
        configuration.frameSemantics = ARFrameSemanticSceneDepth;
    [_sceneView.session runWithConfiguration:configuration];
    qInfo("Measure: AR session started (mode %d, LiDAR %s)", static_cast<int>(_mode), _lidar ? "yes" : "no");
    if (!_displayLink) {
        _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
    [UIApplication sharedApplication].idleTimerDisabled = YES;
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self stopSession];
}

// The display link holds this controller: invalidated here, which every
// way out passes.
- (void)stopSession
{
    [_displayLink invalidate];
    _displayLink = nil;
    [_sceneView.session pause];
    [UIApplication sharedApplication].idleTimerDisabled = NO;
}

// ---- Each frame

- (void)tick:(CADisplayLink*)link
{
    (void)link;
    [self updateHit];
    [self refresh];
}

- (void)updateHit
{
    _hasHit = NO;
    const CGRect bounds = _sceneView.bounds;
    const CGPoint middle = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
    ARRaycastQuery* query = [_sceneView raycastQueryFromPoint:middle
                                               allowingTarget:ARRaycastTargetEstimatedPlane
                                                    alignment:ARRaycastTargetAlignmentAny];
    if (!query)
        return;
    NSArray<ARRaycastResult*>* results = [_sceneView.session raycast:query];
    ARRaycastResult* first = results.firstObject;
    if (!first)
        return;
    const simd_float4 position = first.worldTransform.columns[3];
    _hit = simd_make_float3(position.x, position.y, position.z);
    _hasHit = YES;
    if (!_loggedFirstHit) {
        _loggedFirstHit = YES;
        qInfo("Measure: AR crosshair met a surface at (%.3f, %.3f, %.3f) m", static_cast<double>(_hit.x),
              static_cast<double>(_hit.y), static_cast<double>(_hit.z));
    }
}

- (size_t)maxPoints
{
    return _mode == ArMode::Box ? 4 : 1000;
}

- (BOOL)canFinish
{
    switch (_mode) {
    case ArMode::Distance:
        return _points.size() >= 2;
    case ArMode::Outline:
        return _points.size() >= 3;
    case ArMode::Box:
        return _points.size() == 4;
    }
    return NO;
}

- (NSString*)instructionText
{
    switch (_mode) {
    case ArMode::Distance:
        return @"Aim the crosshair at a point and tap Add point. Each new point measures from the last one.";
    case ArMode::Outline:
        return @"Add points around the shape, all on one flat surface. The outline closes by itself.";
    case ArMode::Box:
        switch (_points.size()) {
        case 0:
            return @"Aim at one corner of the box's base and tap Add point.";
        case 1:
            return @"Now the next corner along one side of the base.";
        case 2:
            return @"Now a corner on the other side of the base.";
        case 3:
            return @"Now aim at the top of the box and add a point there.";
        default:
            return @"Tap Done to keep the box, or Undo to place the last point again.";
        }
    }
    return @"";
}

- (NSString*)readoutText
{
    if (_mode == ArMode::Box) {
        std::vector<simd_float3> points = _points;
        if (points.size() == 4) {
            const Box box = boxFrom(points);
            return [NSString stringWithFormat:@"W %@  D %@  H %@", lengthText(box.width, _inches),
                                              lengthText(box.depth, _inches), lengthText(box.height, _inches)];
        }
        if (!_hasHit)
            return @"Aim at a surface";
        points.push_back(_hit);
        const Box box = boxFrom(points);
        switch (points.size()) {
        case 2:
            return [NSString stringWithFormat:@"W %@", lengthText(box.width, _inches)];
        case 3:
            return [NSString stringWithFormat:@"D %@", lengthText(box.depth, _inches)];
        case 4:
            return [NSString stringWithFormat:@"H %@", lengthText(box.height, _inches)];
        default:
            return @"";
        }
    }
    if (!_hasHit)
        return @"Aim at a surface";
    if (_points.empty())
        return @"";
    return lengthText(lengthBetween(_points.back(), _hit), _inches);
}

- (UILabel*)segmentLabelAt:(NSUInteger)index
{
    while (_segmentLabels.count <= index) {
        UILabel* label = [[UILabel alloc] initWithFrame:CGRectZero];
        label.font = [UIFont monospacedDigitSystemFontOfSize:14 weight:UIFontWeightSemibold];
        label.textColor = UIColor.whiteColor;
        label.textAlignment = NSTextAlignmentCenter;
        label.backgroundColor = panelColor();
        label.layer.cornerRadius = 9;
        label.layer.masksToBounds = YES;
        label.userInteractionEnabled = NO;
        [self.view insertSubview:label belowSubview:_coaching];
        [_segmentLabels addObject:label];
    }
    return _segmentLabels[index];
}

- (void)refresh
{
    const std::vector<Segment> segments = segmentsFor(_mode, _points, _hasHit, _hit, _inches);

    // The segments: pooled nodes, the extra ones hidden.
    while (_segmentNodes.count < segments.size()) {
        SCNNode* node = segmentNode();
        [_sceneView.scene.rootNode addChildNode:node];
        [_segmentNodes addObject:node];
    }
    NSUInteger labelCount = 0;
    for (NSUInteger i = 0; i < _segmentNodes.count; ++i) {
        SCNNode* node = _segmentNodes[i];
        if (i >= segments.size()) {
            node.hidden = YES;
            continue;
        }
        const Segment& segment = segments[i];
        placeSegment(node, segment.a, segment.b);
        node.geometry.firstMaterial.diffuse.contents = segment.live ? liveColor() : UIColor.whiteColor;
        if (!segment.label)
            continue;
        // Its label, where the segment's middle is on the screen.
        const simd_float3 middle = (segment.a + segment.b) * 0.5f;
        const SCNVector3 projected = [_sceneView projectPoint:SCNVector3Make(middle.x, middle.y, middle.z)];
        UILabel* label = [self segmentLabelAt:labelCount];
        ++labelCount;
        if (projected.z < 0 || projected.z > 1) {
            label.hidden = YES; // behind the camera
            continue;
        }
        label.hidden = NO;
        label.text = segment.label;
        label.textColor = segment.live ? liveColor() : UIColor.whiteColor;
        [label sizeToFit];
        CGRect frame = label.frame;
        frame.size.width += 16;
        frame.size.height += 8;
        label.frame = frame;
        label.center = [_sceneView convertPoint:CGPointMake(projected.x, projected.y) toView:self.view];
    }
    for (NSUInteger i = labelCount; i < _segmentLabels.count; ++i)
        _segmentLabels[i].hidden = YES;

    _hitNode.hidden = !_hasHit;
    if (_hasHit)
        _hitNode.simdPosition = _hit;

    _instruction.text = [self instructionText];
    NSString* readout = [self readoutText];
    _readout.text = readout;
    _readoutPill.hidden = readout.length == 0;
    _addButton.enabled = _hasHit && _points.size() < [self maxPoints];
    _undoButton.enabled = !_points.empty();
    _doneButton.enabled = [self canFinish];
}

// ---- Buttons

- (void)addTapped
{
    if (!_hasHit || _points.size() >= [self maxPoints])
        return;
    _points.push_back(_hit);
    SCNNode* node = sphereNode(0.006, UIColor.whiteColor);
    node.simdPosition = _hit;
    [_sceneView.scene.rootNode addChildNode:node];
    [_pointNodes addObject:node];
    impact(UIImpactFeedbackStyleLight);
    [self refresh];
}

- (void)undoTapped
{
    if (_points.empty())
        return;
    _points.pop_back();
    SCNNode* node = _pointNodes.lastObject;
    [node removeFromParentNode];
    if (node)
        [_pointNodes removeLastObject];
    [self refresh];
}

- (void)cancelTapped
{
    ArResult result;
    result.outcome = ArResult::Outcome::Cancelled;
    [self finishWith:result];
}

- (void)doneTapped
{
    if (![self canFinish])
        return;
    ArResult result;
    result.outcome = ArResult::Outcome::Done;
    switch (_mode) {
    case ArMode::Distance:
        for (size_t i = 1; i < _points.size(); ++i)
            result.distancesMm.push_back(lengthBetween(_points[i - 1], _points[i]) * 1000.0);
        break;
    case ArMode::Outline: {
        ARFrame* frame = _sceneView.session.currentFrame;
        simd_float3 camera = simd_make_float3(0, 0, 0);
        if (frame) {
            const simd_float4 position = frame.camera.transform.columns[3];
            camera = simd_make_float3(position.x, position.y, position.z);
        }
        if (!outlineOnPlane(_points, camera, frame != nil, result.outlineMm)) {
            result.outcome = ArResult::Outcome::Failed;
            result.error = QStringLiteral("The points lie on a line, not around a shape. Place them around it.");
        }
        break;
    }
    case ArMode::Box: {
        const Box box = boxFrom(_points);
        result.widthMm = box.width * 1000.0;
        result.depthMm = box.depth * 1000.0;
        result.heightMm = box.height * 1000.0;
        break;
    }
    }
    impact(UIImpactFeedbackStyleMedium);
    [self finishWith:result];
}

- (void)finishWith:(const ArResult&)result
{
    if (_done)
        return;
    _done = YES;
    [self stopSession];
    ArResult copy = result;
    copy.mode = _mode;
    const std::function<void(const ArResult&)> finished = _finished;
    _finished = nullptr;
    qInfo("Measure: AR finished: %s (%d points)%s%s",
          copy.outcome == ArResult::Outcome::Done        ? "done"
          : copy.outcome == ArResult::Outcome::Cancelled ? "cancelled"
                                                         : "failed",
          static_cast<int>(_points.size()), copy.error.isEmpty() ? "" : ": ", qPrintable(copy.error));
    [self dismissViewControllerAnimated:YES completion:nil];
    if (finished)
        finished(copy);
}

// ---- ARSessionObserver (through ARSCNViewDelegate)

- (void)session:(ARSession*)session didFailWithError:(NSError*)error
{
    (void)session;
    NSString* reason = [error.localizedDescription copy];
    const BOOL denied = [error.domain isEqualToString:ARErrorDomain] && error.code == ARErrorCodeCameraUnauthorized;
    dispatch_async(dispatch_get_main_queue(), ^{
        ArResult result;
        result.outcome = ArResult::Outcome::Failed;
        result.error = denied ? os::ui::measureios::cameraDeniedMessage()
                              : QStringLiteral("The camera view stopped: %1").arg(reason ? QString::fromNSString(reason)
                                                                                         : QStringLiteral("no reason given"));
        [self finishWith:result];
    });
}

- (void)session:(ARSession*)session cameraDidChangeTrackingState:(ARCamera*)camera
{
    (void)session;
    qInfo("Measure: AR tracking state %d (reason %d)", static_cast<int>(camera.trackingState),
          static_cast<int>(camera.trackingStateReason));
}

@end

namespace os::ui::measureios {

namespace {

void failLater(const std::function<void(const ArResult&)>& finished, ArMode mode, const QString& message)
{
    qInfo("Measure: AR not started: %s", qPrintable(message));
    const std::function<void(const ArResult&)> done = finished;
    const QString text = message;
    dispatch_async(dispatch_get_main_queue(), ^{
        ArResult result;
        result.outcome = ArResult::Outcome::Failed;
        result.mode = mode;
        result.error = text;
        if (done)
            done(result);
    });
}

void presentAr(QWindow* window, ArMode mode, const QString& lengthUnit,
               const std::function<void(const ArResult&)>& finished)
{
    if (![ARWorldTrackingConfiguration isSupported]) {
        failLater(finished, mode, QStringLiteral("This device can't measure with the camera (it has no ARKit world tracking)."));
        return;
    }
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
        failLater(finished, mode, cameraDeniedMessage());
        return;
    }
    UIView* view = hostView(window);
    UIViewController* presenter = view ? presenterFor(view) : nil;
    if (!presenter) {
        failLater(finished, mode, QStringLiteral("There is no window to show the camera view in."));
        return;
    }
    // ARKit asks for the camera itself when the session starts, if iOS has
    // not asked yet (NSCameraUsageDescription); a refusal fails the session.
    OSArMeasureController* controller =
        [[OSArMeasureController alloc] initWithMode:mode
                                             inches:lengthUnit == QLatin1String("in") ? YES : NO
                                           finished:finished];
    controller.modalPresentationStyle = UIModalPresentationFullScreen;
    [presenter presentViewController:controller animated:YES completion:nil];
}

} // namespace

ArCapabilities arCapabilities()
{
    ArCapabilities capabilities;
    capabilities.available = [ARWorldTrackingConfiguration isSupported];
    capabilities.lidar = capabilities.available && [ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticSceneDepth];
    return capabilities;
}

void startAr(QWindow* window, ArMode mode, const QString& lengthUnit, std::function<void(const ArResult&)> finished)
{
    if ([NSThread isMainThread]) {
        presentAr(window, mode, lengthUnit, finished);
        return;
    }
    const QString unit = lengthUnit;
    const std::function<void(const ArResult&)> finishedCopy = std::move(finished);
    dispatch_async(dispatch_get_main_queue(), ^{
        presentAr(window, mode, unit, finishedCopy);
    });
}

} // namespace os::ui::measureios
