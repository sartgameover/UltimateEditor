# 🎬 UltimateEditor (C++)

Welcome to the **UltimateEditor** repository — a powerful, lightweight, and open-source multimedia editor written in C++!

This project provides a comprehensive solution for video editing, audio processing, screen recording, and much more. The software is designed with a strong focus on high performance and modularity.

## 📑 Table of Contents

1. [About the Project](#-about-the-project)
2. [Key Features](#-key-features)
3. [Fonts and UI Customization](#-fonts-and-ui-customization)
4. [Project Structure](#-project-structure)
5. [Build and Installation](#-build-and-installation)
6. [Contributing](#-contributing)
7. [License](#-license)

## 🚀 About the Project

**UltimateEditor** is not just another video editor. It is a unified tool that brings together compositing, audio mixing, and real-time media capture. By utilizing C++, the editor guarantees fast rendering speeds and low system resource consumption.

## ✨ Key Features

The current version of the editor implements the following core modules:

* 🎞️ **Advanced Timeline (`Timeline`)**: A user-friendly multi-track timeline for video and audio.
* 🎵 **Audio Engine & Mixer (`AudioEngine`, `Mixer`)**: Deep audio processing, supporting audio effects (`AudioFx`) and per-channel adjustments (`ChannelFx`).
* 🎥 **Screen Recording (`ScreenRecorder`)**: A built-in utility to capture your desktop without needing third-party software (like OBS).
* 🎙️ **Voice Recording (`VoiceRecorder`)**: Direct microphone audio recording straight to the timeline for voiceovers.
* 🎨 **Compositing & Effects (`Compositor`, `Effects`)**: Apply filters, manage video layers, add watermarks (`Water`), and visualize audio peaks (`Peaks`).
* 📤 **Powerful Export (`Export`)**: Flexible rendering settings to export your finished project into various media formats.
* ⚙️ **Project Management (`Project`, `AppSettings`)**: Save, load, and configure custom user preferences.

## 🖌️ Fonts and UI Customization

The interface of **UltimateEditor** is fully customizable (see `Theme.cpp` and `Fonts.cpp`). We strive to make the editor look modern and stylish.

To customize the typography in the application, you can use any free fonts. We highly recommend using the [**Google Fonts**](https://fonts.google.com/) library.

**How to add a custom font from Google Fonts:**

1. Go to [fonts.google.com](https://fonts.google.com/).
2. Choose a font you like (e.g., *Roboto*, *Inter*, or *Fira Code* for monospaced elements).
3. Download the `.ttf` or `.otf` file.
4. Place it in your project's resources folder.
5. The program's engine (via the `Fonts` module) will automatically load the new fonts once your theme is properly configured!

## 📁 Project Structure

The repository has a strict and clear architecture:

```
UltimateEditor-cpp/
├── CMakeLists.txt           # Main CMake build configuration file
├── start.sh                 # Magic script: installs dependencies, builds, and runs
├── Image/                   # Icons, logos, and graphic resources
│   └── README.txt
├── packaging/               # Release packaging resources
│   └── uve.desktop          # Linux desktop shortcut
└── src/                     # C++ Source code
    ├── main.cpp             # Application entry point
    ├── MainWindow.cpp/.h    # Main UI Window
    ├── AudioEngine.cpp/.h   # Audio processing core
    ├── Timeline.cpp/.h      # Timeline logic
    ├── ScreenRecorder...    # Screen recording module
    ├── Export.cpp/.h        # Rendering module
    └── ... (and other components)
```

## 🛠️ Build and Installation

We have made the installation process as simple as possible! Our `start.sh` script automatically detects your Linux distribution (Arch, Manjaro, Ubuntu, Debian, Mint, Fedora, Raspberry Pi OS, etc.), downloads all necessary dependencies, compiles the project, and launches the editor.

### Quick Start

1. Clone the repository to your local machine:

   ```bash
   git clone https://github.com/YOUR_USERNAME/UltimateEditor-cpp.git
   cd UltimateEditor-cpp
   ```

2. Make the script executable and run it:

   ```bash
   chmod +x start.sh
   ./start.sh
   ```

And that's it! The script will handle all the heavy lifting for you. Subsequent runs will just launch the application instantly.

## 🤝 Contributing

We are always happy to welcome new contributors! If you want to help improve **UltimateEditor**:

1. Fork this repository.
2. Create a new branch for your feature (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4. Push to your fork (`git push origin feature/AmazingFeature`).
5. Open a **Pull Request** to the original repository.

If you find a bug or have an idea for a new feature, please open an **Issue**!

## 📄 License

This project is distributed under the MIT License (or specify your license here, e.g., GPL-3.0). See the `LICENSE` file for more details.

*Made with ❤️ by the C++ developer community.*