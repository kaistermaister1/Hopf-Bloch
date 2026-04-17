# Hopf-Bloch

Interactive Hopf fibration and Bloch sphere visualizations for exploring how
global phase fibers in `S^3` map down to quantum states on the Bloch sphere.

![Hopf fibration and Bloch sphere screenshot](docs/screenshot.png)

## What is here

This repository has two entry points:

- `src/hopf_fibration.cpp` is the native C++/raylib app. It shows the Hopf
  fiber in `S^3` next to an interactive Bloch sphere picker, with a live
  inverse-map error check.
- `examples/hopf-fibration.html` is a self-contained browser prototype that
  renders the same Hopf/Bloch idea with Three.js.

## Project Structure

```text
.
|-- README.md
|-- docs/
|   `-- screenshot.png        # README screenshot
|-- examples/
|   `-- hopf-fibration.html   # Browser-based Three.js demo
|-- scripts/
|   `-- build.ps1             # Windows build helper for the C++ app
`-- src/
    `-- hopf_fibration.cpp    # Native raylib implementation
```

Generated app files go into `build/`, which is ignored by git.

## Build and Run

The native app is currently set up for Windows with MSYS2 UCRT64, `g++`, and
raylib installed under `C:/msys64/ucrt64`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
.\build\hopf_fibration.exe
```

Run the math check without opening the visualization:

```powershell
.\build\hopf_fibration.exe --check
```

Expected output is a pair of small floating-point errors for the Hopf map and
unit-length spinor checks.

## Controls

- Drag the left view to orbit the projected Hopf fiber.
- Use the mouse wheel on the left view to zoom.
- Drag the point on the Bloch sphere to choose a state.
- Drag empty space on the Bloch sphere to rotate it.
- Hold `Shift` while dragging to pick the far hemisphere.
- Press `F` to flip the selected point across the current Bloch view.
- Press `P` to start or clear path tracing.
- Use the bottom-left button to show or hide the reference fibration cloud.

## Math Convention

For a Bloch vector

```text
n = (sin(theta) cos(phi), sin(theta) sin(phi), cos(theta))
```

the selected representative spinor is

```text
psi = (sin(theta / 2), exp(i phi) cos(theta / 2)).
```

The highlighted Hopf fiber is the global-phase orbit

```text
exp(i gamma) psi, 0 <= gamma < 2 pi.
```
