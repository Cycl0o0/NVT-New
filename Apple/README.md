# NVT Apple App

This folder contains the native SwiftUI client for the existing NVT backend.

Targets:

- `NVT`: a shared SwiftUI app for `iOS`, `macOS`, and `visionOS`
- `NVT Watch`: a watchOS companion app with compact line, stop, and departure browsing

## Generate the Xcode Project

```bash
make apple-project
```

This generates:

```text
Apple/NVT.xcodeproj
```

## Backend URL

The app talks directly to the local NVT backend over HTTP.

- On macOS and simulators, `http://127.0.0.1:8080` works when `nvt-backend` runs on the same machine
- On a physical iPhone, Apple Watch, or Vision Pro, use the LAN IP of the machine running the backend instead

## Suggested Flow

1. Start the backend with `make backend-run`
2. Generate the Xcode project with `make apple-project`
3. Open `Apple/NVT.xcodeproj`
4. Launch `NVT` on macOS, iOS, or visionOS
5. Point the app to the backend URL in Settings if needed
