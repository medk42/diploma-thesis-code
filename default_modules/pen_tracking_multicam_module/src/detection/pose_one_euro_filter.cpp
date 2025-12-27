#include "detection/pose_one_euro_filter.h"

#include <algorithm>
#include <cmath>

#define M_PI 3.14159265358979323846

namespace aergo::default_modules::pen_tracking_multicam_module
{
static inline double clampd(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
static inline double alphaFromCutoffDt(double cutoff_hz, double dt)
{
    cutoff_hz = std::max(1e-6, cutoff_hz);
    dt = std::max(1e-6, dt);
    const double tau = 1.0 / (2.0 * M_PI * cutoff_hz);
    return 1.0 / (1.0 + tau / dt);
}

// ---------------- LowPass ----------------
double PoseOneEuroFilter::LowPass::filter(double x, double a)
{
    a = clampd(a, 0.0, 1.0);
    if (first) { first = false; prev = x; return x; }
    prev = a * x + (1.0 - a) * prev;
    return prev;
}
void PoseOneEuroFilter::LowPass::reset() { first = true; prev = 0.0; }

// ---------------- OneEuroScalar ----------------
double PoseOneEuroFilter::OneEuroScalar::filter(double x, double dt)
{
    dt = std::max(1e-6, dt);

    if (first)
    {
        first = false;
        x_lp.first = true;
        dx_lp.first = true;
        x_lp.prev = x;        // hatxprev = x
        return x;
    }

    const double hatxprev = x_lp.prev;            // previous filtered value
    const double dx = (x - hatxprev) / dt;        // IMPORTANT change

    const double a_d = alphaFromCutoffDt(dcutoff_hz, dt);
    const double edx = dx_lp.filter(dx, a_d);

    const double cutoff = mincutoff_hz + beta * std::abs(edx);
    const double a_x = alphaFromCutoffDt(cutoff, dt);

    return x_lp.filter(x, a_x);
}

void PoseOneEuroFilter::OneEuroScalar::reset()
{
    first = true;
    x_lp.reset();
    dx_lp.reset();
}

// ---------------- OneEuroVec3 ----------------
cv::Vec3d PoseOneEuroFilter::OneEuroVec3::filter(const cv::Vec3d& v, double dt)
{
    return { fx.filter(v[0], dt), fy.filter(v[1], dt), fz.filter(v[2], dt) };
}
void PoseOneEuroFilter::OneEuroVec3::reset() { fx.reset(); fy.reset(); fz.reset(); }

// ---------------- PoseOneEuroFilter ----------------
PoseOneEuroFilter::PoseOneEuroFilter(const Params& p) : params_(p)
{
    // wire params into sub-filters
    trans_filter_.fx.mincutoff_hz = trans_filter_.fy.mincutoff_hz = trans_filter_.fz.mincutoff_hz = params_.trans_mincutoff_hz;
    trans_filter_.fx.beta         = trans_filter_.fy.beta         = trans_filter_.fz.beta         = params_.trans_beta;
    trans_filter_.fx.dcutoff_hz   = trans_filter_.fy.dcutoff_hz   = trans_filter_.fz.dcutoff_hz   = params_.trans_dcutoff_hz;

    reset();
}

void PoseOneEuroFilter::reset()
{
    initialized_ = false;
    stale_after_loss_ = false;
    missed_ = 0;

    trans_filter_.reset();
    rot_omega_lp_.reset();

    q_out_ = cv::Vec4d(1.0, 0.0, 0.0, 0.0);

    last_tick_ = Clock::time_point{};
    last_meas_time_ = Clock::time_point{};
    // last_out_ left as-is (you may still want to hold last pose if you call updateLost() immediately after reset)
}

bool PoseOneEuroFilter::isInitialized() const { return initialized_; }
int  PoseOneEuroFilter::consecutiveMissed() const { return missed_; }

double PoseOneEuroFilter::computeDt_(Clock::time_point now)
{
    if (last_tick_.time_since_epoch().count() == 0)
    {
        last_tick_ = now;
        return 1.0 / 60.0; // reasonable default for first call
    }
    const double dt = std::chrono::duration<double>(now - last_tick_).count();
    last_tick_ = now;
    return clampd(dt, params_.dt_min_sec, params_.dt_max_sec);
}

void PoseOneEuroFilter::markStaleAfterLongLoss_()
{
    // We keep last_out_ for “hold last pose”, but we reset filter internals
    // so the next measurement re-initializes (no laggy catch-up from stale).
    stale_after_loss_ = true;
    initialized_ = false;
    trans_filter_.reset();
}

pu::SE3 PoseOneEuroFilter::updateLost()
{
    const auto now = Clock::now();
    (void)computeDt_(now); // advance time so dt doesn't explode when measurements return

    missed_++;

    const bool have_meas_time = (last_meas_time_.time_since_epoch().count() != 0);
    const double since_meas = have_meas_time ? std::chrono::duration<double>(now - last_meas_time_).count()
                                            : std::numeric_limits<double>::infinity();

    if (missed_ >= params_.max_consecutive_missed || since_meas >= params_.reset_timeout_sec)
    {
        markStaleAfterLongLoss_();
    }

    // Hold last output pose during loss
    return last_out_;
}

void PoseOneEuroFilter::initFromMeasurement_(const pu::SE3& m)
{
    const cv::Matx33d Rm = m.R;
    const cv::Vec3d   tm = m.t;

    q_out_ = quatNormalize_(quatFromR_(Rm));

    // Prime filters to start at measurement (no startup lag)
    trans_filter_.reset();

    // Force internal states to measurement by running filter once with dt
    // (first-call path sets prev)
    (void)trans_filter_.filter(tm, 1.0 / 60.0);

    last_out_ = m;

    initialized_ = true;
    stale_after_loss_ = false;
    missed_ = 0;
}

double PoseOneEuroFilter::quatAngleRad_(const cv::Vec4d& q_unit_in)
{
    // q assumed unit (or close). Ensure shortest: w >= 0
    cv::Vec4d q = quatNormalize_(q_unit_in);
    if (q[0] < 0.0) q = -q;

    const double w = std::clamp(q[0], -1.0, 1.0);
    const double vnorm = std::sqrt(q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    // robust angle: 2*atan2(|v|, w)
    return 2.0 * std::atan2(vnorm, w); // in [0, pi]
}

cv::Vec4d PoseOneEuroFilter::quatSlerp_(cv::Vec4d qa, cv::Vec4d qb, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    qa = quatNormalize_(qa);
    qb = quatNormalize_(qb);

    // shortest path
    double dot = quatDot_(qa, qb);
    if (dot < 0.0) { qb = -qb; dot = -dot; }
    dot = std::clamp(dot, -1.0, 1.0);

    // if very close, nlerp is fine
    if (dot > 0.9995)
    {
        cv::Vec4d q = (1.0 - t) * qa + t * qb;
        return quatNormalize_(q);
    }

    const double theta = std::acos(dot);
    const double s = std::sin(theta);
    const double w0 = std::sin((1.0 - t) * theta) / s;
    const double w1 = std::sin(t * theta) / s;

    return quatNormalize_(w0 * qa + w1 * qb);
}

pu::SE3 PoseOneEuroFilter::update(const pu::SE3& measured_pose)
{
    const auto now = Clock::now();
    const double dt = computeDt_(now);

    // If we were stale after long loss, or never initialized, re-init cleanly.
    if (!initialized_ || stale_after_loss_)
    {
        initFromMeasurement_(measured_pose);
        last_meas_time_ = now;
        last_out_ = measured_pose;
        return last_out_;
    }

    missed_ = 0;
    last_meas_time_ = now;

    // --- translation ---
    const cv::Vec3d t_filt = trans_filter_.filter(measured_pose.t, dt);

    // --- rotation (1€ adaptive slerp) ---
    cv::Vec4d q_meas = quatNormalize_(quatFromR_(measured_pose.R));

    // Ensure quaternion sign continuity (shortest path)
    if (quatDot_(q_meas, q_out_) < 0.0) q_meas = -q_meas;

    // angular speed estimate from relative rotation
    const cv::Vec4d q_err = quatMul_(quatConj_(q_out_), q_meas);
    const double theta = quatAngleRad_(q_err);                 // rad
    const double omega = theta / std::max(1e-6, dt);           // rad/s

    // smooth omega using rot_dcutoff
    const double a_omega = alphaFromCutoffDt(params_.rot_dcutoff_hz, dt);
    const double eomega  = rot_omega_lp_.filter(omega, a_omega);

    // adaptive cutoff -> alpha
    const double fc = params_.rot_mincutoff_hz + params_.rot_beta * std::abs(eomega);
    const double a_rot = alphaFromCutoffDt(fc, dt);

    // filter the rotation *directly* (cannot overshoot)
    q_out_ = quatSlerp_(q_out_, q_meas, a_rot);

    const cv::Matx33d R_filt = RFromQuat_(q_out_);

    last_out_ = { R_filt, t_filt };
    return last_out_;
}

// ---------------- quaternion ops ----------------
cv::Vec4d PoseOneEuroFilter::quatNormalize_(const cv::Vec4d& q)
{
    const double n = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (n <= 1e-12) return cv::Vec4d(1,0,0,0);
    return (1.0 / n) * q;
}

cv::Vec4d PoseOneEuroFilter::quatConj_(const cv::Vec4d& q) { return cv::Vec4d(q[0], -q[1], -q[2], -q[3]); }

cv::Vec4d PoseOneEuroFilter::quatMul_(const cv::Vec4d& a, const cv::Vec4d& b)
{
    // (w1,v1)*(w2,v2) = (w1*w2 - v1·v2, w1*v2 + w2*v1 + v1×v2)
    const double w1=a[0], x1=a[1], y1=a[2], z1=a[3];
    const double w2=b[0], x2=b[1], y2=b[2], z2=b[3];
    return {
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2
    };
}

double PoseOneEuroFilter::quatDot_(const cv::Vec4d& a, const cv::Vec4d& b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
}

cv::Vec4d PoseOneEuroFilter::quatFromR_(const cv::Matx33d& R)
{
    // Robust matrix->quat (assumes proper rotation).
    const double tr = R(0,0) + R(1,1) + R(2,2);
    cv::Vec4d q;

    if (tr > 0.0)
    {
        const double s = std::sqrt(tr + 1.0) * 2.0; // s = 4*w
        q[0] = 0.25 * s;
        q[1] = (R(2,1) - R(1,2)) / s;
        q[2] = (R(0,2) - R(2,0)) / s;
        q[3] = (R(1,0) - R(0,1)) / s;
    }
    else if (R(0,0) > R(1,1) && R(0,0) > R(2,2))
    {
        const double s = std::sqrt(1.0 + R(0,0) - R(1,1) - R(2,2)) * 2.0; // s = 4*x
        q[0] = (R(2,1) - R(1,2)) / s;
        q[1] = 0.25 * s;
        q[2] = (R(0,1) + R(1,0)) / s;
        q[3] = (R(0,2) + R(2,0)) / s;
    }
    else if (R(1,1) > R(2,2))
    {
        const double s = std::sqrt(1.0 + R(1,1) - R(0,0) - R(2,2)) * 2.0; // s = 4*y
        q[0] = (R(0,2) - R(2,0)) / s;
        q[1] = (R(0,1) + R(1,0)) / s;
        q[2] = 0.25 * s;
        q[3] = (R(1,2) + R(2,1)) / s;
    }
    else
    {
        const double s = std::sqrt(1.0 + R(2,2) - R(0,0) - R(1,1)) * 2.0; // s = 4*z
        q[0] = (R(1,0) - R(0,1)) / s;
        q[1] = (R(0,2) + R(2,0)) / s;
        q[2] = (R(1,2) + R(2,1)) / s;
        q[3] = 0.25 * s;
    }
    return quatNormalize_(q);
}

cv::Matx33d PoseOneEuroFilter::RFromQuat_(const cv::Vec4d& q_in)
{
    const cv::Vec4d q = quatNormalize_(q_in);
    const double w=q[0], x=q[1], y=q[2], z=q[3];

    const double ww=w*w, xx=x*x, yy=y*y, zz=z*z;
    return cv::Matx33d(
        ww + xx - yy - zz, 2.0*(x*y - w*z),     2.0*(x*z + w*y),
        2.0*(x*y + w*z),   ww - xx + yy - zz,   2.0*(y*z - w*x),
        2.0*(x*z - w*y),   2.0*(y*z + w*x),     ww - xx - yy + zz
    );
}

cv::Vec3d PoseOneEuroFilter::quatLogSO3_(const cv::Vec4d& q_unit_in)
{
    cv::Vec4d q = quatNormalize_(q_unit_in);

    // Ensure shortest rotation (w >= 0)
    if (q[0] < 0.0) q = -q;

    const cv::Vec3d v(q[1], q[2], q[3]);
    const double nv = std::sqrt(v.dot(v));
    const double w  = clampd(q[0], -1.0, 1.0);

    if (nv < 1e-12)
    {
        // small angle: rotvec ~ 2*v
        return 2.0 * v;
    }

    const double theta = 2.0 * std::atan2(nv, w); // in [0, pi]
    return (theta / nv) * v; // axis * angle
}

cv::Vec4d PoseOneEuroFilter::quatExpSO3_(const cv::Vec3d& rotvec)
{
    const double theta = std::sqrt(rotvec.dot(rotvec));
    if (theta < 1e-12)
    {
        // small angle: q ~ [1, rotvec/2]
        return quatNormalize_(cv::Vec4d(1.0, rotvec[0]*0.5, rotvec[1]*0.5, rotvec[2]*0.5));
    }

    const double half = 0.5 * theta;
    const double s = std::sin(half) / theta;
    return quatNormalize_(cv::Vec4d(std::cos(half), rotvec[0]*s, rotvec[1]*s, rotvec[2]*s));
}

} // namespace
