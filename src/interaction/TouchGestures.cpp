#include "interaction/TouchGestures.h"

#include <cmath>

namespace os::interact {

void TouchGestureRecognizer::reset()
{
    active_ = false;
    maxFingers_ = 0;
    moved_ = false;
    pointerDown_ = false;
    navigating_ = false;
    twoPhase_ = false;
    start_.clear();
}

std::vector<TouchIntent> TouchGestureRecognizer::update(const std::vector<TouchPoint>& points, double seconds)
{
    std::vector<TouchIntent> out;
    if (points.empty())
        return out;
    if (!active_) {
        active_ = true;
        startTime_ = seconds;
    }

    // Fingers still down in this frame (released ones only count for movement).
    std::vector<const TouchPoint*> down;
    for (const auto& p : points) {
        if (!start_.contains(p.id))
            start_[p.id] = p.position;
        if ((p.position - start_[p.id]).length() > kTapMovement)
            moved_ = true;
        if (p.state != TouchPoint::State::Released)
            down.push_back(&p);
    }
    const int fingers = static_cast<int>(down.size());
    if (fingers > maxFingers_)
        maxFingers_ = fingers;

    // A second finger turns a pending one-finger press into a gesture.
    if (maxFingers_ >= 2 && pointerDown_) {
        out.push_back({TouchIntent::Kind::PointerCancel, {}, {}, {}, 1.0});
        pointerDown_ = false;
    }

    if (maxFingers_ == 1) {
        // One finger points: press, move, release; quick still taps may pair
        // up into a double-tap.
        const TouchPoint& p = points.front();
        TouchIntent intent;
        intent.position = p.position;
        if (p.state == TouchPoint::State::Pressed) {
            intent.kind = TouchIntent::Kind::PointerPress;
            pointerDown_ = true;
            out.push_back(intent);
        } else if (p.state == TouchPoint::State::Released) {
            if (pointerDown_) {
                intent.kind = TouchIntent::Kind::PointerRelease;
                out.push_back(intent);
                pointerDown_ = false;
                if (!moved_ && seconds - startTime_ <= kTapDuration) {
                    if (seconds - lastTapTime_ <= kDoubleTapGap
                        && (p.position - lastTapPosition_).length() <= kDoubleTapDistance) {
                        out.push_back({TouchIntent::Kind::DoubleTap, p.position, {}, {}, 1.0});
                        lastTapTime_ = -1e9;
                    } else {
                        lastTapTime_ = seconds;
                        lastTapPosition_ = p.position;
                    }
                }
            }
        } else if (pointerDown_) {
            intent.kind = TouchIntent::Kind::PointerMove;
            out.push_back(intent);
        }
    } else if (fingers >= 2) {
        // Two fingers navigate, but only once they really move: a quick
        // two-finger tap must not nudge the view.
        const Vec2 a = down[0]->position, b = down[1]->position;
        const Vec2 centroid = (a + b) * 0.5;
        const double span = (a - b).length();
        if (!twoPhase_) {
            twoPhase_ = true;
            navStartCentroid_ = centroid;
            navStartSpan_ = span;
            if (navigating_) { // a finger came back: continue from here, no jump
                lastCentroid_ = centroid;
                lastSpan_ = span;
            }
        }
        if (!navigating_
            && ((centroid - navStartCentroid_).length() > kNavigateStart || std::abs(span - navStartSpan_) > kNavigateStart)) {
            navigating_ = true;
            moved_ = true;
            lastCentroid_ = navStartCentroid_; // include the movement that crossed the threshold
            lastSpan_ = navStartSpan_;
        }
        if (navigating_) {
            out.push_back({TouchIntent::Kind::Pan, centroid, lastCentroid_, centroid, 1.0});
            if (lastSpan_ > 1 && span > 1)
                out.push_back({TouchIntent::Kind::Pinch, centroid, {}, {}, span / lastSpan_});
            lastCentroid_ = centroid;
            lastSpan_ = span;
        }
    } else {
        twoPhase_ = false; // back to fewer than two fingers
    }

    // All fingers lifted: the sequence ends. A quick, still tap with two or
    // three fingers undoes or redoes.
    if (fingers == 0) {
        const bool quick = seconds - startTime_ <= kTapDuration && !moved_;
        if (quick && maxFingers_ == 2)
            out.push_back({TouchIntent::Kind::Undo, {}, {}, {}, 1.0});
        else if (quick && maxFingers_ == 3)
            out.push_back({TouchIntent::Kind::Redo, {}, {}, {}, 1.0});
        reset();
    }
    return out;
}

} // namespace os::interact
