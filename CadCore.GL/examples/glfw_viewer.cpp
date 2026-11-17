// =============================================================================
// CadCore.GL / examples/glfw_viewer.cpp
//
// Stand-alone host for the engine using GLFW. It exists to show (and test)
// that CadCore.GL has no Windows dependency: the same Viewer that the WPF
// application drives through the C++/CLI bridge runs here on Linux / macOS.
//
// Mouse : left drag = orbit, right or middle drag = pan, wheel = zoom
// Keys  : 1 ortho  2 perspective  3 one-point  4 two-point
//         L lighting mode   E display mode   B bold edges   G grid 1 m / 10 m
//         F fit   I isometric   Esc quit
//
// Command line (used for automated screenshots):
//   --shot out.ppm   render one frame to a PPM image and exit
//   --proj N         0 ortho, 1 perspective, 2 one-point, 3 two-point
//   --light N        0 ambient only, 1 two lights
//   --display N      0 shaded, 1 shaded+edges, 2 wireframe
//   --grid MM        grid size in mm          --zoom N   wheel notches
//   --orbit DX DY    orbit by pixels          --bold     3.5 px edges
//   --stl FILE       also load an STL file
// =============================================================================
#include "CadCore/Viewer.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace CadCore;

namespace {

/// IGLContext implemented with GLFW.
class GlfwContext : public IGLContext {
public:
    GlfwContext(GLFWwindow* w, bool swap) : m_window(w), m_swap(swap) {}
    void MakeCurrent() override { glfwMakeContextCurrent(m_window); }
    void SwapBuffers() override { if (m_swap) glfwSwapBuffers(m_window); }
private:
    GLFWwindow* m_window;
    bool m_swap;
};

struct App {
    Viewer* viewer = nullptr;
    double lastX = 0, lastY = 0;
    bool bold = false;
    bool bigGrid = false;
};

// ---- demo geometry -----------------------------------------------------------

/// Box from (0,0,0) to (sx,sy,sz) the way a B-Rep tessellator delivers it:
/// four vertices per face (flat normals) and the 12 topological edges.
void MakeBox(float sx, float sy, float sz, std::vector<float>& pos, std::vector<float>& nrm,
             std::vector<std::uint32_t>& idx, std::vector<float>& edges) {
    const float c[8][3] = {{0, 0, 0}, {sx, 0, 0}, {sx, sy, 0}, {0, sy, 0},
                           {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz}};
    const int   faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                               {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}};
    for (int f = 0; f < 6; ++f) {
        std::uint32_t base = static_cast<std::uint32_t>(pos.size() / 3);
        for (int v = 0; v < 4; ++v) {
            pos.insert(pos.end(), c[faces[f][v]], c[faces[f][v]] + 3);
            nrm.insert(nrm.end(), normals[f], normals[f] + 3);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& s : e) {
        edges.insert(edges.end(), c[s[0]], c[s[0]] + 3);
        edges.insert(edges.end(), c[s[1]], c[s[1]] + 3);
    }
}

/// Cylinder along Z. No edges are supplied: the engine derives the two rim
/// circles as feature edges and leaves the smooth wall free of lines.
void MakeCylinder(float radius, float height, int seg, std::vector<float>& pos,
                  std::vector<float>& nrm, std::vector<std::uint32_t>& idx) {
    auto add = [&](float x, float y, float z, float nx, float ny, float nz) {
        pos.insert(pos.end(), {x, y, z});
        nrm.insert(nrm.end(), {nx, ny, nz});
        return static_cast<std::uint32_t>(pos.size() / 3 - 1);
    };
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        std::uint32_t a = add(radius * c0, radius * s0, 0, c0, s0, 0);
        std::uint32_t b = add(radius * c1, radius * s1, 0, c1, s1, 0);
        std::uint32_t c = add(radius * c1, radius * s1, height, c1, s1, 0);
        std::uint32_t d = add(radius * c0, radius * s0, height, c0, s0, 0);
        idx.insert(idx.end(), {a, b, c, a, c, d});
        std::uint32_t t0 = add(0, 0, height, 0, 0, 1);
        std::uint32_t t1 = add(radius * c0, radius * s0, height, 0, 0, 1);
        std::uint32_t t2 = add(radius * c1, radius * s1, height, 0, 0, 1);
        idx.insert(idx.end(), {t0, t1, t2});
        std::uint32_t b0 = add(0, 0, 0, 0, 0, -1);
        std::uint32_t b1 = add(radius * c1, radius * s1, 0, 0, 0, -1);
        std::uint32_t b2 = add(radius * c0, radius * s0, 0, 0, 0, -1);
        idx.insert(idx.end(), {b0, b1, b2});
    }
}

void BuildDemoScene(Viewer& v) {
    // Scene tree:  Root -> "Assembly" (group) -> box, cylinder, glass cover
    //                   -> "Scan" (point cloud)
    int assembly = v.AddGroup(Scene::kRootId, "Assembly");

    std::vector<float> pos, nrm, edges;
    std::vector<std::uint32_t> idx;
    MakeBox(120, 80, 40, pos, nrm, idx, edges);
    int box = v.AddMesh(assembly, "Base plate (B-Rep)", MeshKind::BRep, pos.data(), nrm.data(),
                        pos.size() / 3, idx.data(), idx.size(), edges.data(), edges.size() / 6);
    v.SetNodeMaterial(box, Material::Steel());

    pos.clear(); nrm.clear(); idx.clear();
    MakeCylinder(22, 70, 48, pos, nrm, idx);
    int cyl = v.AddMesh(assembly, "Boss", MeshKind::Generic, pos.data(), nrm.data(),
                        pos.size() / 3, idx.data(), idx.size());
    v.SetNodeMaterial(cyl, Material::Brass());
    Mat4 t = Mat4::Translation({60, 40, 40});
    v.SetNodeTransform(cyl, t.m);

    pos.clear(); nrm.clear(); idx.clear(); edges.clear();
    MakeBox(60, 100, 60, pos, nrm, idx, edges);
    int cover = v.AddMesh(assembly, "Cover (glass)", MeshKind::BRep, pos.data(), nrm.data(),
                          pos.size() / 3, idx.data(), idx.size(), edges.data(), edges.size() / 6);
    v.SetNodeMaterial(cover, Material::Glass());
    t = Mat4::Translation({140, -10, 0});
    v.SetNodeTransform(cover, t.m);

    // Point cloud: a wavy patch coloured by height.
    std::vector<float> pts, cols;
    for (int i = 0; i < 120; ++i)
        for (int j = 0; j < 120; ++j) {
            float x = -150.0f + i * 1.0f, y = 120.0f + j * 1.0f;
            float z = 20.0f + 12.0f * std::sin(i * 0.12f) * std::cos(j * 0.10f);
            pts.insert(pts.end(), {x, y, z});
            float k = (z - 8.0f) / 24.0f;
            cols.insert(cols.end(), {k, 0.4f, 1.0f - k});
        }
    v.AddPointCloud(Scene::kRootId, "Scan", pts.data(), cols.data(), pts.size() / 3, 3.0f);
}

void SavePpm(const char* path, int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)   // OpenGL rows run bottom-up
        out.write(reinterpret_cast<const char*>(px.data() + static_cast<std::size_t>(y) * w * 3), w * 3);
}

} // namespace

int main(int argc, char** argv) {
    const char* shot = nullptr;
    const char* stl = nullptr;
    int proj = 1, light = 1, display = 1;
    float grid = 1000.0f, zoom = 0.0f, orbitX = 0.0f, orbitY = 0.0f;
    bool bold = false;
    for (int i = 1; i < argc; ++i) {
        auto is = [&](const char* s) { return std::strcmp(argv[i], s) == 0; };
        if (is("--shot") && i + 1 < argc) shot = argv[++i];
        else if (is("--stl") && i + 1 < argc) stl = argv[++i];
        else if (is("--proj") && i + 1 < argc) proj = std::atoi(argv[++i]);
        else if (is("--light") && i + 1 < argc) light = std::atoi(argv[++i]);
        else if (is("--display") && i + 1 < argc) display = std::atoi(argv[++i]);
        else if (is("--grid") && i + 1 < argc) grid = static_cast<float>(std::atof(argv[++i]));
        else if (is("--zoom") && i + 1 < argc) zoom = static_cast<float>(std::atof(argv[++i]));
        else if (is("--orbit") && i + 2 < argc) {
            orbitX = static_cast<float>(std::atof(argv[++i]));
            orbitY = static_cast<float>(std::atof(argv[++i]));
        }
        else if (is("--bold")) bold = true;
    }

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }

    // Ask for the newest core context first, then fall back to 4.5.
    GLFWwindow* window = nullptr;
    for (int minor : {6, 5}) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        if (shot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(1280, 800, "CadCore.GL - GLFW host", nullptr, nullptr);
        if (window) break;
    }
    if (!window) { std::fprintf(stderr, "No OpenGL 4.5+ context available\n"); return 1; }

    int exitCode = 0;
    try {
        GlfwContext context(window, shot == nullptr);
        Viewer viewer(&context);
        viewer.Initialize();
        std::printf("OpenGL: %s\n", viewer.GLVersion().c_str());

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        viewer.Resize(w, h);

        BuildDemoScene(viewer);
        if (stl) viewer.AddStlFile(Scene::kRootId, stl, "STL");

        RenderSettings& s = viewer.Settings();
        s.gridSizeMm = grid;
        s.displayMode = static_cast<DisplayMode>(display);
        s.lighting.mode = static_cast<LightingMode>(light);
        if (bold) s.edgeWidthPx = 3.5f;
        viewer.SetProjection(static_cast<ProjectionMode>(proj));
        viewer.FitAll();
        viewer.Orbit(orbitX, orbitY);
        viewer.Zoom(zoom);

        if (shot) {
            viewer.Render();
            SavePpm(shot, w, h);
        } else {
            App app;
            app.viewer = &viewer;
            glfwSetWindowUserPointer(window, &app);

            glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int fw, int fh) {
                static_cast<App*>(glfwGetWindowUserPointer(win))->viewer->Resize(fw, fh);
            });
            glfwSetScrollCallback(window, [](GLFWwindow* win, double, double dy) {
                static_cast<App*>(glfwGetWindowUserPointer(win))->viewer->Zoom(static_cast<float>(dy));
            });
            glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
                App* a = static_cast<App*>(glfwGetWindowUserPointer(win));
                float dx = static_cast<float>(x - a->lastX), dy = static_cast<float>(y - a->lastY);
                a->lastX = x; a->lastY = y;
                if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) a->viewer->Orbit(dx, dy);
                else if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
                         glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) a->viewer->Pan(dx, dy);
            });
            glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int, int action, int) {
                if (action != GLFW_PRESS) return;
                App* a = static_cast<App*>(glfwGetWindowUserPointer(win));
                Viewer& v = *a->viewer;
                RenderSettings& rs = v.Settings();
                switch (key) {
                case GLFW_KEY_1: v.SetProjection(ProjectionMode::Orthographic); break;
                case GLFW_KEY_2: v.SetProjection(ProjectionMode::Perspective); break;
                case GLFW_KEY_3: v.SetProjection(ProjectionMode::OnePoint); break;
                case GLFW_KEY_4: v.SetProjection(ProjectionMode::TwoPoint); break;
                case GLFW_KEY_L:
                    rs.lighting.mode = rs.lighting.mode == LightingMode::Lights
                        ? LightingMode::AmbientOnly : LightingMode::Lights;
                    break;
                case GLFW_KEY_E:
                    rs.displayMode = static_cast<DisplayMode>((static_cast<int>(rs.displayMode) + 1) % 3);
                    break;
                case GLFW_KEY_B: a->bold = !a->bold; rs.edgeWidthPx = a->bold ? 3.5f : 1.5f; break;
                case GLFW_KEY_G: a->bigGrid = !a->bigGrid; rs.gridSizeMm = a->bigGrid ? 10000.0f : 1000.0f; break;
                case GLFW_KEY_F: v.FitAll(); break;
                case GLFW_KEY_I: v.SetStandardView(StandardView::Isometric); break;
                case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, GLFW_TRUE); break;
                default: break;
                }
            });

            glfwSwapInterval(1);
            while (!glfwWindowShouldClose(window)) {
                viewer.Render();
                glfwWaitEventsTimeout(0.05);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        exitCode = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
