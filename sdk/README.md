# FileMaker Plug-In SDK

The Claris FileMaker Plug-In SDK headers and pre-built FMWrapper libraries
are extracted here from `fm_plugin_sdk_26.0.1.51.zip`.

    sdk/
    ├── FMWrapper/          — C++ header files
    │   ├── FMXExtern.h
    │   ├── FMXTypes.h
    │   ├── FMXText.h
    │   ├── FMXData.h
    │   ├── FMXCalcEngine.h
    │   ├── FMXBinaryData.h
    │   ├── FMXFixPt.h
    │   └── FMXDateTime.h
    └── Libraries/
        ├── Mac/FMWrapper.framework   — linked on macOS
        ├── Win/x64/FMWrapper.lib     — linked on Windows
        └── Linux/
            ├── U22/x64/libFMWrapper.so
            ├── U22/arm64/libFMWrapper.so
            ├── U24/x64/libFMWrapper.so
            └── U24/arm64/libFMWrapper.so

These files are covered by the Claris SDK licence agreement and are excluded
from version control via .gitignore.  To restore them, download:
https://downloads.claris.com/DEVREL/sdk/fm_plugin_sdk_26.0.1.51.zip
and re-extract into this directory.
