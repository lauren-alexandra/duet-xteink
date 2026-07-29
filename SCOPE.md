# Project Vision and Scope: Duet

Duet is a reading-first firmware for the Xteink X3 and X4. It builds on CrossInk and CrossPoint Reader while exploring richer library navigation, typography, reading analytics, accessibility, and explicit two-device sync. It is created and maintained by Lauren Landau as an independent, one-person project.

## 1. Core Mission

Provide a responsive, reliable, highly configurable reading experience that respects the ESP32-C3's memory limits and works coherently on both X3 and X4.

## 2. Scope

### In-Scope

_These are features that directly improve the primary purpose of the device._

- **User Experience:** E.g. User-friendly interfaces, and interactions, both inside the reader and navigating the firmware. This includes things like button mapping, book loading, and book navigation like bookmarks.
- **Document Rendering:** E.g. Support for rendering documents (primarily EPUB) and improvements to the rendering engine.
- **Format Optimization:** E.g. Efficiently parsing EPUB (CSS/Images) and other documents within the device's capabilities.
- **Typography & Legibility:** E.g. Custom font support, hyphenation engines, and adjustable line spacing.
- **E-Ink Driver Refinement:** E.g. Reducing full-screen flashes (ghosting management) and improving general rendering.
- **Library Management:** E.g. Simple, intuitive ways to organize and navigate a collection of books.
- **Local Transfer:** E.g. Simple, "pull" based book loading via a basic web-server or public and widely-used standards.
- **Language Support:** E.g. Support for multiple languages both in the reader and in the interfaces.
- **Reference Tools:** E.g. Local dictionary lookup. Providing quick, offline definitions to enhance comprehension without breaking focus.
- **Clock Display (device dependent):**

| Device | Scope |
| --- | --- |
| X3 | The X3 uses its available RTC path for wall-clock display. Duet also retains a CRC-protected Stats Date so daily history remains repairable. |
| X4 | The X4's internal clock can drift during deep sleep. Duet provides on-demand NTP date/time sync when Wi-Fi is connected and keeps the separate CRC-protected Stats Date for reading-history grouping; it does not require background connectivity. |

### Out-of-Scope

_These items are rejected because they compromise the device's stability or mission._

- **General-purpose productivity suites:** Duet may include a small, intentionally bounded diversion such as Tetris, but it is not a PDA.
- **Active Connectivity:** No RSS readers, News aggregators, or Web browsers. Background Wi-Fi tasks drain the battery and complicate the single-core CPU's execution.
- **Media Playback:** No Audio players or Audiobooks.
- **Complex Annotation:** No typed out notes. These features are better suited for devices with better input capabilities and more powerful chips.

### In-scope — Technically Unsupported

_These features align with Crosspoint's goals but are impractical on the current hardware or produce poor UX._

- **PDF Rendering:** PDFs are fixed-layout documents, so rendering them requires displaying pages as images rather than reflowable text — resulting in constant panning and zooming that makes for a poor reading experience on e-ink.

## 3. Idea Evaluation

Features that compromise device stability or reading reliability will not be accepted into the official release. Ask whether the idea improves reading, library discovery, accessibility, device safety, or useful reflection on reading without overwhelming the hardware.

> **Note to Contributors:** If you are unsure if your idea fits the scope, please open a **Discussion** before you start coding!
