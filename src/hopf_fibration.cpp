#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

namespace {

constexpr int INITIAL_W = 1500;
constexpr int INITIAL_H = 900;
constexpr int FIBER_STEPS = 384;
constexpr int CLOUD_LONGITUDE_SAMPLES = 168;
constexpr int CLOUD_PHASE_SAMPLES = 44;
constexpr int PATH_MAX_SAMPLES = 160;
constexpr int PATH_FIBER_STEPS = 96;
constexpr float PATH_SAMPLE_DISTANCE = 0.026f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HTML_REFERENCE_LATITUDE = 0.70710678118f;  // cos(pi/4), matching example.html eta = pi/8.

struct TrackQuat {
    float w;
    float x;
    float y;
    float z;
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct ProjectionBasis {
    Vec4 ex;
    Vec4 ey;
    Vec4 ez;
    Vec4 ew;
};

struct CloudPoint {
    Vector3 position;
    Color color;
};

struct DragState {
    bool draggingBloch = false;
    bool rotatingBloch = false;
    float hemisphere = -1.0f;
};

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

float ClampUnit(float v) {
    return std::clamp(v, -1.0f, 1.0f);
}

Color LerpColor(Color a, Color b, float t) {
    t = Clamp01(t);
    return Color{
        static_cast<unsigned char>(a.r + (b.r - a.r) * t),
        static_cast<unsigned char>(a.g + (b.g - a.g) * t),
        static_cast<unsigned char>(a.b + (b.b - a.b) * t),
        static_cast<unsigned char>(a.a + (b.a - a.a) * t)
    };
}

Color BlochColor(Vector3 n) {
    float hue = std::atan2(n.y, n.x) / TWO_PI;
    if (hue < 0.0f) hue += 1.0f;
    const float value = 0.86f + 0.10f * (0.5f + 0.5f * n.z);
    Color c = ColorFromHSV(360.0f * hue, 0.72f, value);
    return LerpColor(c, WHITE, 0.10f);
}

TrackQuat QuatIdentity() {
    return TrackQuat{1.0f, 0.0f, 0.0f, 0.0f};
}

TrackQuat QuatConjugate(const TrackQuat& q) {
    return TrackQuat{q.w, -q.x, -q.y, -q.z};
}

TrackQuat QuatNormalize(const TrackQuat& q) {
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-8f) return QuatIdentity();
    const float inv = 1.0f / n;
    return TrackQuat{q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}

TrackQuat QuatMul(const TrackQuat& a, const TrackQuat& b) {
    return TrackQuat{
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
    };
}

TrackQuat QuatFromAxisAngleLocal(Vector3 axis, float angle) {
    const float len = Vector3Length(axis);
    if (len < 1e-8f) return QuatIdentity();
    axis = Vector3Scale(axis, 1.0f / len);

    const float half = 0.5f * angle;
    const float s = std::sin(half);
    return TrackQuat{std::cos(half), axis.x * s, axis.y * s, axis.z * s};
}

Vector3 QuatRotateVectorLocal(const TrackQuat& qRaw, const Vector3& v) {
    const TrackQuat q = QuatNormalize(qRaw);
    const TrackQuat p{0.0f, v.x, v.y, v.z};
    const TrackQuat r = QuatMul(QuatMul(q, p), QuatConjugate(q));
    return Vector3{r.x, r.y, r.z};
}

void CameraFrameBasis(const Camera3D& camera, Vector3& right, Vector3& up, Vector3& forward) {
    Vector3 offset = Vector3Subtract(camera.position, camera.target);
    if (Vector3Length(offset) < 0.0001f) offset = {0.0f, -4.0f, 0.0f};

    up = camera.up;
    if (Vector3Length(up) < 0.0001f) up = {0.0f, 0.0f, 1.0f};
    up = Vector3Normalize(up);

    forward = Vector3Normalize(Vector3Negate(offset));
    right = Vector3Normalize(Vector3CrossProduct(forward, up));
    up = Vector3Normalize(Vector3CrossProduct(right, forward));
}

void StepRootTrackballCamera(
    Camera3D& camera,
    Rectangle viewport,
    bool inputBlocked,
    bool forceRotate = false,
    bool useWheel = true,
    float minR = 0.05f,
    float maxR = 60.0f
) {
    const float rotSpeed = 0.01f;
    const float zoomSpeed = 0.12f;

    const Vector2 mouse = GetMousePosition();
    const bool inViewport = CheckCollisionPointRec(mouse, viewport) && !inputBlocked;

    Vector3 offset = Vector3Subtract(camera.position, camera.target);
    if (Vector3Length(offset) < 0.0001f) offset = {0.0f, -5.0f, 0.0f};

    Vector3 up = camera.up;
    if (Vector3Length(up) < 0.0001f) up = {0.0f, 0.0f, 1.0f};
    up = Vector3Normalize(up);

    if ((inViewport && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) || forceRotate) {
        const Vector2 d = GetMouseDelta();

        const TrackQuat qYaw = QuatFromAxisAngleLocal(up, -rotSpeed * d.x);
        offset = QuatRotateVectorLocal(qYaw, offset);
        up = QuatRotateVectorLocal(qYaw, up);

        Vector3 forward = Vector3Normalize(Vector3Negate(offset));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));

        const TrackQuat qPitch = QuatFromAxisAngleLocal(right, -rotSpeed * d.y);
        offset = QuatRotateVectorLocal(qPitch, offset);
        up = QuatRotateVectorLocal(qPitch, up);
    }

    float r = Vector3Length(offset);
    const float wheel = (inViewport && useWheel) ? GetMouseWheelMove() : 0.0f;
    if (wheel != 0.0f) {
        r *= std::exp(-zoomSpeed * wheel);
        r = std::clamp(r, minR, maxR);
        offset = Vector3Scale(Vector3Normalize(offset), r);
    }

    camera.position = Vector3Add(camera.target, offset);

    Vector3 forward = Vector3Normalize(Vector3Negate(offset));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));
    up = Vector3Normalize(Vector3CrossProduct(right, forward));
    camera.up = up;
}

void BlochCameraBasis(const Camera3D& blochCamera, Vector3& right, Vector3& up, Vector3& forward) {
    CameraFrameBasis(blochCamera, right, up, forward);
}

void StepBlochOrbit(Camera3D& blochCamera, bool rotating) {
    if (!rotating) return;

    StepRootTrackballCamera(
        blochCamera,
        Rectangle{0.0f, 0.0f, 0.0f, 0.0f},
        false,
        true,
        false,
        3.0f,
        6.0f
    );
}

float Dot4(Vec4 a, Vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

Vec4 Sub4(Vec4 a, Vec4 b) {
    return Vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

Vec4 Scale4(Vec4 v, float s) {
    return Vec4{v.x * s, v.y * s, v.z * s, v.w * s};
}

Vec4 Normalize4(Vec4 v) {
    const float len = std::sqrt(std::max(Dot4(v, v), 0.0f));
    if (len < 1e-8f) return Vec4{1.0f, 0.0f, 0.0f, 0.0f};
    return Scale4(v, 1.0f / len);
}

Vec4 OrthoAgainst(Vec4 v, const std::vector<Vec4>& basis) {
    for (Vec4 e : basis) {
        v = Sub4(v, Scale4(e, Dot4(v, e)));
    }
    return Normalize4(v);
}

ProjectionBasis BuildProjectionBasis() {
    ProjectionBasis b{};
    b.ew = Normalize4(Vec4{0.31f, -0.47f, 0.72f, 0.40f});
    b.ex = OrthoAgainst(Vec4{1.0f, 0.0f, 0.0f, 0.0f}, {b.ew});
    b.ey = OrthoAgainst(Vec4{0.0f, 1.0f, 0.0f, 0.0f}, {b.ew, b.ex});
    b.ez = OrthoAgainst(Vec4{0.0f, 0.0f, 1.0f, 0.0f}, {b.ew, b.ex, b.ey});
    return b;
}

Vector3 NormalizeBloch(Vector3 n) {
    const float len = Vector3Length(n);
    if (len < 1e-6f) return Vector3{1.0f, 0.0f, 0.0f};
    return Vector3Scale(n, 1.0f / len);
}

Vec4 HtmlSpinorOnFiber(Vector3 bloch, float globalPhase) {
    bloch = NormalizeBloch(bloch);

    const float theta = std::acos(ClampUnit(bloch.z));
    const float phi = std::atan2(bloch.y, bloch.x);
    const float eta = 0.5f * theta;
    const float s = std::sin(eta);
    const float c = std::cos(eta);

    // Matches example.html: (sin eta e^(i alpha), cos eta e^(i(alpha + phi))).
    return Vec4{
        s * std::cos(globalPhase),
        s * std::sin(globalPhase),
        c * std::cos(globalPhase + phi),
        c * std::sin(globalPhase + phi)
    };
}

Vector3 HtmlHopfMap(Vec4 spinor) {
    const float x1 = spinor.x;
    const float x2 = spinor.y;
    const float x3 = spinor.z;
    const float x4 = spinor.w;

    // example.html maps to (sin theta cos phi, sin theta sin phi, cos theta).
    return Vector3{
        2.0f * (x1 * x3 + x2 * x4),
        2.0f * (x1 * x4 - x2 * x3),
        x3 * x3 + x4 * x4 - x1 * x1 - x2 * x2
    };
}

Vector3 GoldenSpherePoint(int i, int count);

Vector3 ProjectS3ToR3(Vec4 p, const ProjectionBasis& basis) {
    (void)basis;
    const float denom = std::max(0.045f, 1.0001f - p.w);
    const float scale = 1.20f;
    return Vector3{scale * p.x / denom, scale * p.y / denom, scale * p.z / denom};
}

float HopfInverseError(Vector3 bloch) {
    Vec4 p = HtmlSpinorOnFiber(bloch, 1.137f);
    Vector3 h = HtmlHopfMap(p);
    Vector3 d = Vector3Subtract(NormalizeBloch(bloch), h);
    return Vector3Length(d);
}

int RunMathCheck() {
    float maxHopfError = 0.0f;
    float maxS3Error = 0.0f;

    for (int i = 0; i < 1200; ++i) {
        const Vector3 n = GoldenSpherePoint(i, 1200);
        for (int j = 0; j < 7; ++j) {
            const float gamma = TWO_PI * static_cast<float>(j) / 7.0f;
            const Vec4 p = HtmlSpinorOnFiber(n, gamma);
            const float s3Norm = std::sqrt(Dot4(p, p));
            const Vector3 mapped = HtmlHopfMap(p);
            maxS3Error = std::max(maxS3Error, std::fabs(s3Norm - 1.0f));
            maxHopfError = std::max(maxHopfError, Vector3Length(Vector3Subtract(n, mapped)));
        }
    }

    std::printf("max |h(exp(i gamma) psi) - n| = %.8e\n", maxHopfError);
    std::printf("max ||psi||_S3 - 1 error      = %.8e\n", maxS3Error);
    return (maxHopfError < 2e-5f && maxS3Error < 2e-5f) ? 0 : 1;
}

void DrawTubeSegment(Vector3 a, Vector3 b, float radius, Color color) {
    if (Vector3Distance(a, b) < 1e-5f) return;
    DrawCylinderEx(a, b, radius, radius, 8, color);
}

std::vector<Vector3> BuildFiberPoints(Vector3 bloch, const ProjectionBasis& basis, int steps) {
    std::vector<Vector3> points;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        points.push_back(ProjectS3ToR3(HtmlSpinorOnFiber(bloch, TWO_PI * t), basis));
    }
    return points;
}

void AddPathSample(std::vector<Vector3>& pathTrace, Vector3 bloch) {
    bloch = NormalizeBloch(bloch);
    if (!pathTrace.empty() && Vector3Distance(pathTrace.back(), bloch) < PATH_SAMPLE_DISTANCE) {
        return;
    }

    pathTrace.push_back(bloch);
    if (pathTrace.size() > PATH_MAX_SAMPLES) {
        pathTrace.erase(pathTrace.begin());
    }
}

Vector3 GoldenSpherePoint(int i, int count) {
    const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float phi = static_cast<float>(i) * 2.39996323f;
    return Vector3{r * std::cos(phi), r * std::sin(phi), z};
}

Vector3 BlochLatitudePoint(float z, float phi) {
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Vector3{r * std::cos(phi), r * std::sin(phi), z};
}

std::vector<CloudPoint> BuildReferenceHopfCloud(const ProjectionBasis& basis) {
    std::vector<CloudPoint> cloud;
    cloud.reserve(3 * CLOUD_LONGITUDE_SAMPLES * CLOUD_PHASE_SAMPLES);

    const float latitudes[3] = {-HTML_REFERENCE_LATITUDE, 0.0f, HTML_REFERENCE_LATITUDE};
    for (int band = 0; band < 3; ++band) {
        const float z = latitudes[band];
        for (int i = 0; i < CLOUD_LONGITUDE_SAMPLES; ++i) {
            const float phi = TWO_PI * static_cast<float>(i) / CLOUD_LONGITUDE_SAMPLES;
            const Vector3 n = BlochLatitudePoint(z, phi);
            const Color base = BlochColor(n);

            for (int j = 0; j < CLOUD_PHASE_SAMPLES; ++j) {
                const float phase = TWO_PI * static_cast<float>(j) / CLOUD_PHASE_SAMPLES;
                Color c = LerpColor(base, WHITE, 0.06f + 0.16f * static_cast<float>(j) / CLOUD_PHASE_SAMPLES);
                c.a = (band == 1) ? 56 : 43;
                cloud.push_back(CloudPoint{ProjectS3ToR3(HtmlSpinorOnFiber(n, phase), basis), c});
            }
        }
    }

    return cloud;
}

void DrawReferenceLatitudeTori(const ProjectionBasis& basis, float time) {
    const float latitudes[3] = {-HTML_REFERENCE_LATITUDE, 0.0f, HTML_REFERENCE_LATITUDE};
    const float phases[3] = {0.0f, TWO_PI / 3.0f, 2.0f * TWO_PI / 3.0f};

    for (int band = 0; band < 3; ++band) {
        for (int phaseIdx = 0; phaseIdx < 3; ++phaseIdx) {
            Vector3 prev{};
            bool hasPrev = false;
            for (int i = 0; i <= 192; ++i) {
                const float u = static_cast<float>(i) / 192.0f;
                const Vector3 n = BlochLatitudePoint(latitudes[band], TWO_PI * u);
                const float phase = phases[phaseIdx] + 0.10f * std::sin(0.7f * time + band);
                const Vector3 p = ProjectS3ToR3(HtmlSpinorOnFiber(n, phase), basis);
                if (hasPrev) {
                    Color c = BlochColor(n);
                    c.a = (band == 1) ? 88 : 66;
                    DrawLine3D(prev, p, c);
                }
                prev = p;
                hasPrev = true;
            }
        }
    }
}

void DrawPathFibers(const std::vector<Vector3>& pathTrace, const ProjectionBasis& basis) {
    if (pathTrace.size() < 2) return;

    for (size_t s = 0; s < pathTrace.size(); ++s) {
        const std::vector<Vector3> fiber = BuildFiberPoints(pathTrace[s], basis, PATH_FIBER_STEPS);
        Color c = BlochColor(pathTrace[s]);
        c.a = static_cast<unsigned char>(68 + 58 * static_cast<float>(s) / std::max<size_t>(1, pathTrace.size() - 1));

        for (size_t i = 0; i + 1 < fiber.size(); ++i) {
            DrawLine3D(fiber[i], fiber[i + 1], c);
        }
    }
}

void DrawHopfScene(
    Vector3 bloch,
    const ProjectionBasis& basis,
    const std::vector<CloudPoint>& cloud,
    const std::vector<Vector3>& pathTrace,
    bool showDefaultFibrations,
    float time
) {
    ClearBackground(Color{9, 12, 18, 255});

    BeginBlendMode(BLEND_ALPHA);
    if (showDefaultFibrations) {
        for (const CloudPoint& point : cloud) {
            DrawPoint3D(point.position, point.color);
        }
        DrawReferenceLatitudeTori(basis, time);
    }
    DrawPathFibers(pathTrace, basis);

    const Color selected = BlochColor(bloch);
    const std::vector<Vector3> fiber = BuildFiberPoints(bloch, basis, FIBER_STEPS);
    for (size_t i = 0; i + 1 < fiber.size(); ++i) {
        const float u = static_cast<float>(i) / (fiber.size() - 1);
        Color c = LerpColor(selected, WHITE, 0.16f + 0.20f * std::sin(TWO_PI * u + time * 1.3f));
        c.a = 238;
        DrawTubeSegment(fiber[i], fiber[i + 1], 0.034f, c);
    }

    const float beadPhase = std::fmod(0.11f * time, 1.0f);
    const int beadIndex = std::clamp(static_cast<int>(beadPhase * FIBER_STEPS), 0, FIBER_STEPS);
    DrawSphere(fiber[beadIndex], 0.105f, WHITE);
    DrawSphere(fiber[beadIndex], 0.074f, selected);
    EndBlendMode();
}

Rectangle BlochSphereRect(Rectangle panel) {
    const float r = std::min(panel.width * 0.36f, panel.height * 0.35f);
    const float cx = panel.x + panel.width * 0.50f;
    const float cy = panel.y + panel.height * 0.50f;
    return Rectangle{cx - r, cy - r, 2.0f * r, 2.0f * r};
}

Vector2 ProjectBlochToScreen(Vector3 n, Rectangle sphere, const Camera3D& blochCamera) {
    const float r = sphere.width * 0.5f;
    const float cx = sphere.x + r;
    const float cy = sphere.y + r;

    Vector3 right{};
    Vector3 up{};
    Vector3 forward{};
    BlochCameraBasis(blochCamera, right, up, forward);
    return Vector2{cx + Vector3DotProduct(n, right) * r, cy - Vector3DotProduct(n, up) * r};
}

bool PickBlochPoint(Vector2 mouse, Rectangle sphere, const Camera3D& blochCamera, float hemisphere, Vector3& blochOut) {
    const float r = sphere.width * 0.5f;
    const float cx = sphere.x + r;
    const float cy = sphere.y + r;
    float x = (mouse.x - cx) / r;
    float z = -(mouse.y - cy) / r;
    const float len2 = x * x + z * z;
    if (len2 > 1.0f) {
        const float invLen = 1.0f / std::sqrt(len2);
        x *= invLen;
        z *= invLen;
    }
    const float depth = hemisphere * std::sqrt(std::max(0.0f, 1.0f - x * x - z * z));

    Vector3 right{};
    Vector3 up{};
    Vector3 forward{};
    BlochCameraBasis(blochCamera, right, up, forward);
    blochOut = Vector3Add(
        Vector3Add(Vector3Scale(right, x), Vector3Scale(up, z)),
        Vector3Scale(forward, depth)
    );
    return len2 <= 1.08f;
}

void DrawBlochCircle3D(Vector3 center, Vector3 axisA, Vector3 axisB, float radius, Color color) {
    Vector3 prev{};
    bool hasPrev = false;
    for (int i = 0; i <= 144; ++i) {
        const float t = TWO_PI * static_cast<float>(i) / 144.0f;
        Vector3 p = Vector3Add(
            center,
            Vector3Add(
                Vector3Scale(axisA, radius * std::cos(t)),
                Vector3Scale(axisB, radius * std::sin(t))
            )
        );
        if (hasPrev) DrawLine3D(prev, p, color);
        prev = p;
        hasPrev = true;
    }
}

void DrawBlochPath(const std::vector<Vector3>& pathTrace) {
    if (pathTrace.size() < 2) return;

    for (size_t i = 0; i + 1 < pathTrace.size(); ++i) {
        Color c = BlochColor(pathTrace[i + 1]);
        c.a = static_cast<unsigned char>(125 + 85 * static_cast<float>(i + 1) / std::max<size_t>(1, pathTrace.size() - 1));
        DrawLine3D(pathTrace[i], pathTrace[i + 1], c);
    }
}

void DrawBottomRightNote(int width, int height, bool pathTracing) {
    const char* line1 = pathTracing ? "P: path tracing ON" : "P: path tracing";
    const char* line2 = pathTracing ? "Press P again to erase" : "Trace appears on sphere and fibers";
    const int fontSize = 18;
    const int pad = 22;
    const int w = std::max(MeasureText(line1, fontSize), MeasureText(line2, fontSize));
    const int x = width - w - pad;
    const int y = height - 58;
    const Color accent = pathTracing ? Color{146, 245, 190, 255} : Color{194, 207, 214, 255};
    DrawText(line1, x, y, fontSize, accent);
    DrawText(line2, x, y + 24, fontSize, Color{170, 188, 198, 235});
}

void DrawDefaultFibrationsButton(Rectangle rect, bool showDefaultFibrations, bool hovered) {
    const Color fill = showDefaultFibrations
        ? (hovered ? Color{35, 88, 73, 235} : Color{24, 68, 57, 220})
        : (hovered ? Color{78, 54, 58, 235} : Color{55, 40, 45, 220});
    const Color border = showDefaultFibrations ? Color{146, 245, 190, 235} : Color{245, 146, 154, 220};
    const Color text = showDefaultFibrations ? Color{208, 255, 226, 255} : Color{255, 214, 218, 255};
    const char* label = showDefaultFibrations ? "Default fibrations: ON" : "Default fibrations: OFF";

    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, 1.0f, border);
    DrawText(label, static_cast<int>(rect.x + 14.0f), static_cast<int>(rect.y + 11.0f), 18, text);
}

struct BlochMarker {
    const char* label;
    Vector3 position;
    Color color;
};

const BlochMarker BLOCH_MARKERS[] = {
    {"N", Vector3{0.0f, 0.0f, 1.0f}, Color{248, 83, 83, 255}},
    {"S", Vector3{0.0f, 0.0f, -1.0f}, Color{84, 141, 255, 255}},
    {"+", Vector3{1.0f, 0.0f, 0.0f}, Color{255, 149, 64, 255}},
    {"-", Vector3{-1.0f, 0.0f, 0.0f}, Color{255, 224, 92, 255}},
    {"i", Vector3{0.0f, 1.0f, 0.0f}, Color{198, 112, 255, 255}},
};

void DrawBlochMarkers3D() {
    for (const BlochMarker& marker : BLOCH_MARKERS) {
        DrawSphere(marker.position, 0.062f, marker.color);
    }
}

void DrawBlochMarkerKey(int width) {
    const int fontSize = 17;
    const int itemW = 48;
    const int markerCount = static_cast<int>(sizeof(BLOCH_MARKERS) / sizeof(BLOCH_MARKERS[0]));
    const int totalW = itemW * markerCount;
    int x = std::max(28, width - totalW - 28);
    const int y = 34;

    DrawRectangle(x - 14, y - 14, totalW + 24, 34, Color{7, 10, 15, 165});
    for (const BlochMarker& marker : BLOCH_MARKERS) {
        DrawCircle(x, y + 2, 6.0f, marker.color);
        DrawText(marker.label, x + 12, y - 7, fontSize, Color{222, 233, 236, 255});
        x += itemW;
    }
}

void DrawBlochScene3D(
    int width,
    int height,
    Vector3 bloch,
    const Camera3D& blochCamera,
    const std::vector<Vector3>& pathTrace,
    bool pathTracing,
    float hemisphere,
    float hopfError
) {
    ClearBackground(Color{13, 17, 23, 255});

    const Rectangle panel{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    const Rectangle sphere = BlochSphereRect(panel);
    const float r = sphere.width * 0.5f;

    Camera3D blochCam = blochCamera;
    blochCam.fovy = static_cast<float>(height) / std::max(1.0f, r);
    blochCam.projection = CAMERA_ORTHOGRAPHIC;

    BeginMode3D(blochCam);
    BeginBlendMode(BLEND_ALPHA);
    DrawSphereWires(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 12, 24, Color{216, 235, 232, 34});
    DrawBlochCircle3D(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, 1.0f, Color{216, 235, 232, 135});
    const float htmlRingRadius = std::sqrt(1.0f - HTML_REFERENCE_LATITUDE * HTML_REFERENCE_LATITUDE);
    DrawBlochCircle3D(Vector3{0.0f, 0.0f, HTML_REFERENCE_LATITUDE}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, htmlRingRadius, Color{216, 235, 232, 105});
    DrawBlochCircle3D(Vector3{0.0f, 0.0f, -HTML_REFERENCE_LATITUDE}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, htmlRingRadius, Color{216, 235, 232, 105});
    DrawBlochPath(pathTrace);
    DrawBlochMarkers3D();

    const Color selected = BlochColor(bloch);
    DrawSphere(bloch, 0.055f, WHITE);
    DrawSphere(bloch, 0.040f, selected);
    EndBlendMode();
    EndMode3D();

    const Vector2 p = ProjectBlochToScreen(bloch, sphere, blochCamera);
    DrawCircleLines(static_cast<int>(p.x), static_cast<int>(p.y), 13.0f, WHITE);

    const int tx = 28;
    int ty = 28;
    DrawText("Bloch sphere", tx, ty, 30, RAYWHITE);
    ty += 42;
    DrawText("Hold Shift while dragging the point for the far side.", tx, ty, 18, Color{194, 207, 214, 255});
    DrawBlochMarkerKey(width);

    const int infoY = std::min(static_cast<int>(sphere.y + sphere.height + 30.0f), height - 96);
    DrawText(TextFormat("n = (%.3f, %.3f, %.3f)", bloch.x, bloch.y, bloch.z), tx, infoY, 20, selected);
    DrawText(TextFormat("h(psi) error: %.2e", hopfError), tx, infoY + 28, 18, Color{191, 210, 213, 255});
    DrawText(TextFormat("side: %s", (hemisphere < 0.0f) ? "near hemisphere" : "far hemisphere"), tx, infoY + 54, 18, Color{191, 210, 213, 255});
    DrawBottomRightNote(width, height, pathTracing);
}

struct AppState {
    ProjectionBasis basis{};
    std::vector<CloudPoint> hopfCloud;
    Vector3 bloch{};
    DragState drag;
    bool pathTracing = false;
    bool showDefaultFibrations = true;
    std::vector<Vector3> pathTrace;
    Camera3D camera{};
    Camera3D blochCamera{};
    int leftW = 0;
    int rightW = 0;
    int targetH = 0;
    RenderTexture2D leftTarget{};
    RenderTexture2D rightTarget{};
    bool targetsLoaded = false;
};

AppState app;

void LoadPanelTargets(AppState& state, int leftW, int rightW, int targetH) {
    if (state.targetsLoaded) {
        UnloadRenderTexture(state.leftTarget);
        UnloadRenderTexture(state.rightTarget);
    }

    state.leftW = leftW;
    state.rightW = rightW;
    state.targetH = targetH;
    state.leftTarget = LoadRenderTexture(state.leftW, state.targetH);
    state.rightTarget = LoadRenderTexture(state.rightW, state.targetH);
    SetTextureFilter(state.leftTarget.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(state.rightTarget.texture, TEXTURE_FILTER_BILINEAR);
    state.targetsLoaded = true;
}

void InitApp(AppState& state) {
    state.basis = BuildProjectionBasis();
    state.hopfCloud = BuildReferenceHopfCloud(state.basis);
    state.bloch = NormalizeBloch(Vector3{0.58f, -0.48f, 0.66f});
    state.pathTrace.clear();

    state.camera.position = {5.0f, 3.0f, 5.0f};
    state.camera.target = {0.0f, 0.0f, 0.0f};
    state.camera.up = {0.0f, 1.0f, 0.0f};
    state.camera.fovy = 45.0f;
    state.camera.projection = CAMERA_PERSPECTIVE;

    state.blochCamera.position = {0.0f, -4.0f, 0.0f};
    state.blochCamera.target = {0.0f, 0.0f, 0.0f};
    state.blochCamera.up = {0.0f, 0.0f, 1.0f};
    state.blochCamera.fovy = 4.0f;
    state.blochCamera.projection = CAMERA_ORTHOGRAPHIC;

    const int leftW = std::max(320, static_cast<int>(GetScreenWidth() * 0.62f));
    const int rightW = std::max(320, GetScreenWidth() - leftW);
    const int targetH = std::max(320, GetScreenHeight());
    LoadPanelTargets(state, leftW, rightW, targetH);
}

void UpdateDrawFrame() {
    const int screenW = std::max(900, GetScreenWidth());
    const int screenH = std::max(620, GetScreenHeight());
    const int nextLeftW = std::max(360, static_cast<int>(screenW * 0.62f));
    const int nextRightW = std::max(320, screenW - nextLeftW);
    if (nextLeftW != app.leftW || nextRightW != app.rightW || screenH != app.targetH) {
        LoadPanelTargets(app, nextLeftW, nextRightW, screenH);
    }

    const Rectangle leftViewport{0.0f, 0.0f, static_cast<float>(app.leftW), static_cast<float>(screenH)};
    const Rectangle rightPanel{static_cast<float>(app.leftW), 0.0f, static_cast<float>(screenW - app.leftW), static_cast<float>(screenH)};
    const Rectangle sphere = BlochSphereRect(rightPanel);
    const Rectangle defaultButton{24.0f, static_cast<float>(screenH - 62), 252.0f, 40.0f};

    const Vector2 mouse = GetMousePosition();
    const bool overSphere = CheckCollisionPointCircle(mouse, Vector2{sphere.x + sphere.width * 0.5f, sphere.y + sphere.height * 0.5f}, sphere.width * 0.5f);
    const bool overDefaultButton = CheckCollisionPointRec(mouse, defaultButton);

    if (IsKeyPressed(KEY_P)) {
        app.pathTracing = !app.pathTracing;
        app.pathTrace.clear();
        if (app.pathTracing) {
            AddPathSample(app.pathTrace, app.bloch);
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && overDefaultButton) {
        app.showDefaultFibrations = !app.showDefaultFibrations;
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && overSphere) {
        const Vector2 selectedScreen = ProjectBlochToScreen(app.bloch, sphere, app.blochCamera);
        if (Vector2Distance(mouse, selectedScreen) <= 28.0f) {
            app.drag.draggingBloch = true;
        } else {
            app.drag.rotatingBloch = true;
        }
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        app.drag.draggingBloch = false;
        app.drag.rotatingBloch = false;
    }
    if (IsKeyPressed(KEY_F)) {
        Vector3 right{};
        Vector3 up{};
        Vector3 forward{};
        BlochCameraBasis(app.blochCamera, right, up, forward);
        app.bloch = NormalizeBloch(Vector3Subtract(app.bloch, Vector3Scale(forward, 2.0f * Vector3DotProduct(app.bloch, forward))));
        app.drag.hemisphere = (Vector3DotProduct(app.bloch, forward) <= 0.0f) ? -1.0f : 1.0f;
    }
    if (app.drag.draggingBloch) {
        app.drag.hemisphere = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 1.0f : -1.0f;
        PickBlochPoint(mouse, sphere, app.blochCamera, app.drag.hemisphere, app.bloch);
        if (app.pathTracing) {
            AddPathSample(app.pathTrace, app.bloch);
        }
    } else {
        Vector3 right{};
        Vector3 up{};
        Vector3 forward{};
        BlochCameraBasis(app.blochCamera, right, up, forward);
        app.drag.hemisphere = (Vector3DotProduct(app.bloch, forward) <= 0.0f) ? -1.0f : 1.0f;
    }

    StepRootTrackballCamera(app.camera, leftViewport, overDefaultButton, false, true, 3.5f, 18.0f);
    StepBlochOrbit(app.blochCamera, app.drag.rotatingBloch);

    BeginTextureMode(app.leftTarget);
    BeginMode3D(app.camera);
    DrawHopfScene(app.bloch, app.basis, app.hopfCloud, app.pathTrace, app.showDefaultFibrations, static_cast<float>(GetTime()));
    EndMode3D();
    DrawRectangle(18, 16, 350, 62, Color{7, 10, 15, 180});
    DrawText("Hopf fiber in S3", 32, 28, 30, RAYWHITE);
    EndTextureMode();

    BeginTextureMode(app.rightTarget);
    DrawBlochScene3D(app.rightW, app.targetH, app.bloch, app.blochCamera, app.pathTrace, app.pathTracing, app.drag.hemisphere, HopfInverseError(app.bloch));
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(
        app.leftTarget.texture,
        Rectangle{0.0f, 0.0f, static_cast<float>(app.leftTarget.texture.width), -static_cast<float>(app.leftTarget.texture.height)},
        leftViewport,
        Vector2{0.0f, 0.0f},
        0.0f,
        WHITE
    );
    DrawTexturePro(
        app.rightTarget.texture,
        Rectangle{0.0f, 0.0f, static_cast<float>(app.rightTarget.texture.width), -static_cast<float>(app.rightTarget.texture.height)},
        rightPanel,
        Vector2{0.0f, 0.0f},
        0.0f,
        WHITE
    );
    DrawRectangle(app.leftW - 1, 0, 2, screenH, Color{65, 77, 90, 255});
    DrawDefaultFibrationsButton(defaultButton, app.showDefaultFibrations, overDefaultButton);
    EndDrawing();
}

#if !defined(PLATFORM_WEB)
void ShutdownApp(AppState& state) {
    if (state.targetsLoaded) {
        UnloadRenderTexture(state.leftTarget);
        UnloadRenderTexture(state.rightTarget);
        state.targetsLoaded = false;
    }
    CloseWindow();
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--check") {
        return RunMathCheck();
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(INITIAL_W, INITIAL_H, "Hopf Fibration: Bloch Sphere Fiber Picker");
    SetTargetFPS(60);
    InitApp(app);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }
    ShutdownApp(app);
#endif

    return 0;
}
