# New Machine Setup Guide

This guide covers everything needed to build, sign, and distribute the AMQP FM Plugin from a fresh macOS development machine. Work through the sections in order — each section builds on the previous one.

---

## 1. System prerequisites

### Xcode Command Line Tools

Required for the C++ compiler and build tools on macOS.

```bash
xcode-select --install
```

### Homebrew

If not already installed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Core build tools

```bash
brew install cmake git gh
```

- **cmake** 3.21+ — build system
- **git** — source control and used by CMake's FetchContent to pull rabbitmq-c
- **gh** — GitHub CLI, used by `sign-win.sh` to download Windows build artifacts

### Docker Desktop

Required for Linux builds. Download from [docker.com/products/docker-desktop](https://www.docker.com/products/docker-desktop) and start it before running any Linux builds.

---

## 2. Clone the repository

```bash
git clone <repo-url>
cd "MSDev AMQP FM Plugin"
```

---

## 3. FileMaker Plugin SDK

The SDK is covered by the Claris licence and is not committed to the repository. It must be obtained separately and placed in `sdk/`.

**Obtain the SDK:**
Download FileMaker Plugin SDK 26 from the [Claris Developer portal](https://store.claris.com/developers). Extract the archive and copy the relevant folders into `sdk/`:

```
sdk/
  FMWrapper/
    FMWrapper/
      FMXExtern.h
      FMXTypes.h
      ... (all headers)
  Libraries/
    Mac/
      FMWrapper.framework/
    Win/
      x64/
        FMWrapper.lib
    Linux/
      U22/
        x64/
          libFMWrapper.so
        arm64/
          libFMWrapper.so
      U24/
        x64/
          libFMWrapper.so
        arm64/
          libFMWrapper.so
```

The `sdk/` folder is gitignored, so this step must be repeated on every new machine.

---

## 4. macOS code signing setup

This section only applies to machines that will sign and distribute the macOS plugin. If you are only building for development/testing, you can skip it and use ad-hoc signing instead:

```bash
CODESIGN_IDENTITY="-" scripts/build-mac.sh
```

### 4a. Developer ID Application certificate

The signing certificate is issued by Apple to **Matatiro Solutions Limited (RM5TNT52M5)**.

**Install the certificate:**

1. Open **Keychain Access**
2. Import the `.p12` certificate file (obtain from the team's certificate store or export from a machine that already has it)
3. Enter the export password when prompted
4. Ensure it lands in the **login** keychain

Verify the certificate is installed:

```bash
security find-identity -v -p codesigning
```

You should see:

```
1) 8898265DFE686CEE8CF158511F5943B990B0D83D "Developer ID Application: Matatiro Solutions Limited (RM5TNT52M5)"
```

### 4b. Notarization credentials

Notarization requires an App Store Connect API key or Apple ID with an app-specific password. Store credentials in Keychain once with:

```bash
xcrun notarytool store-credentials "AC_PASSWORD" \
    --apple-id steve@msdev.nz \
    --team-id RM5TNT52M5 \
    --password <app-specific-password>
```

The app-specific password is generated at [appleid.apple.com](https://appleid.apple.com) → Sign-In and Security → App-Specific Passwords.

Once stored, `scripts/notarize-mac.sh` will use these credentials automatically.

---

## 5. macOS build

### 5a. Build static OpenSSL (first time only)

OpenSSL must be built as a universal (arm64 + x86_64) static library and placed in `third_party/openssl/mac/`. This only needs to be done once — the output is committed to the repository.

If the `third_party/openssl/mac/lib/libssl.a` file is already present after cloning, skip this step.

```bash
scripts/build-openssl-mac.sh
```

This downloads OpenSSL 3.3.2, builds it for both architectures, lipo-merges the results, and places the libraries in `third_party/openssl/mac/`. Takes about 5 minutes.

### 5b. Build the plugin

```bash
scripts/build-mac.sh
```

This configures and builds a universal `.fmplugin`, signs it with the Developer ID Application certificate (see section 4 for certificate setup), and verifies the signature.

Output: `build/mac/AMQPFMPlugin.fmplugin`

### 5c. Notarize

After building, submit to Apple's notarization service:

```bash
scripts/notarize-mac.sh
```

This zips the bundle, submits it, waits for approval (~1–5 minutes), and staples the ticket. Requires notarization credentials to be stored in Keychain first — see section 4b.

### 5d. Install for testing

```bash
cp -R build/mac/AMQPFMPlugin.fmplugin \
  "$HOME/Library/Application Support/FileMaker/Extensions/"
```

Quit FileMaker before installing, then relaunch.

---

## 6. Linux build (via Docker)

Docker must be running. No other Linux-specific setup is needed on the Mac.

```bash
# Build for Ubuntu 22.04 and 24.04, x64 (both)
scripts/build-linux-docker.sh

# Single target options:
scripts/build-linux-docker.sh u22          # Ubuntu 22.04, x64
scripts/build-linux-docker.sh u24          # Ubuntu 24.04, x64
scripts/build-linux-docker.sh u22 arm64    # Ubuntu 22.04, arm64
```

On first run, Docker builds the container image and compiles static OpenSSL inside it. Both are cached — subsequent runs are much faster.

Output: `build/linux/U22/x64/AMQPFMPlugin.fmx` etc.

---

## 7. Windows build (GitHub Actions)

The Windows plugin is built by the `build-windows.yml` GitHub Actions workflow on every push to `main`. You do not need a Windows machine.

### 7a. GitHub CLI authentication

```bash
gh auth login
```

Follow the prompts to authenticate with GitHub. This is required for `scripts/sign-win.sh` to download artifacts.

### 7b. FileMaker SDK release asset (one-time, per repository)

The FileMaker SDK cannot be committed (Claris licence). It is stored as an asset on a private pre-release tagged `sdk-assets`. The workflow downloads it automatically using the built-in `GITHUB_TOKEN` — no manual secret is needed.

To set it up (run once from the repo root):

```bash
zip -r sdk.zip sdk/
gh release create sdk-assets sdk.zip \
    --repo matatirosolutions/fm-amqp-plugin \
    --title "Build Assets" \
    --notes "SDK and other build assets not suitable for committing" \
    --prerelease
rm sdk.zip
```

To update the SDK in future:

```bash
zip -r sdk.zip sdk/
gh release upload sdk-assets sdk.zip --clobber
rm sdk.zip
```

This only needs to be done once per repository (not per machine).

### 7c. Trigger a build

Push to `main`, or trigger manually:

```bash
gh workflow run build-windows.yml
```

---

## 8. Windows code signing setup

Signing the Windows `.fmx64` uses a PKCS11 hardware token (SafeNet/Thales eToken) via `osslsigncode` running on macOS. The unsigned binary is downloaded from GitHub Actions and signed locally.

### 8a. Install signing tools

```bash
brew install osslsigncode libp11 opensc
```

- **osslsigncode** — Authenticode signing tool
- **libp11** — PKCS11 engine for OpenSSL (provides `pkcs11.dylib`)
- **opensc** — provides `pkcs11-tool` for token inspection

### 8b. Install SafeNet Authentication Client (SAC)

The eToken driver is not available via Homebrew. Download **SafeNet Authentication Client** for macOS from the Thales customer portal and run the installer.

The installer places the PKCS11 module at:
```
/Library/Frameworks/eToken.framework/Versions/Current/libeToken.dylib
```

### 8c. Plug in the eToken and verify

With the token plugged in, list its contents:

```bash
pkcs11-tool \
  --module /Library/Frameworks/eToken.framework/Versions/Current/libeToken.dylib \
  --login \
  --list-objects
```

Enter the token PIN when prompted. You should see certificates and keys for `Matatiro Solutions Limited` and `Verokey Secure Code`.

If you get `CKR_PIN_EXPIRED`, open SafeNet Authentication Client and change the PIN before continuing.

### 8d. Set environment variables

Add to `~/.zprofile`:

```bash
export SIGN_MODULE="/Library/Frameworks/eToken.framework/Versions/Current/libeToken.dylib"
export SIGN_TOKEN="MatatiroSolutions"
export SIGN_OBJECT="Matatiro Solutions Limited"
export SIGN_PASS="your-token-pin"
```

Then reload:

```bash
source ~/.zprofile
```

### 8e. Extract the CA certificate (first time only)

The Verokey intermediate CA certificate needs to be extracted from the token and committed to the repository so it can be embedded in signatures. This file (`resources/win/verokey-ca.crt`) should already be present after cloning. If for some reason it is missing:

```bash
scripts/extract-signing-ca.sh
git add resources/win/verokey-ca.crt
git commit -m "Add Verokey intermediate CA certificate"
```

### 8f. Sign a Windows build

Once the GitHub Actions build for `main` has completed:

```bash
scripts/sign-win.sh
```

This will:
1. Find the latest completed `build-windows.yml` run on `main`
2. Wait for it if still running
3. Download the unsigned `AMQPFMPlugin.fmx64` artifact
4. Sign it with the eToken using `osslsigncode`
5. Verify the signature

Output: `build/win/signed/AMQPFMPlugin.fmx64`

---

## 9. Quick reference — build commands

| Platform | Command | Output |
|---|---|---|
| macOS (build + sign) | `scripts/build-mac.sh` | `build/mac/AMQPFMPlugin.fmplugin` |
| macOS (notarize) | `scripts/notarize-mac.sh` | stapled in place |
| Linux (all targets) | `scripts/build-linux-docker.sh` | `build/linux/*/AMQPFMPlugin.fmx` |
| Windows (sign artifact) | `scripts/sign-win.sh` | `build/win/signed/AMQPFMPlugin.fmx64` |

---

## 10. Troubleshooting

**`codesign: errSecInternalComponent`** — The signing certificate is in the login keychain but the keychain is locked. Open Keychain Access and unlock the login keychain, or run `security unlock-keychain ~/Library/Keychains/login.keychain-db`.

**`CKR_PIN_EXPIRED` from pkcs11-tool** — The eToken PIN has expired. Open SafeNet Authentication Client to change it.

**`Error: environment variable SIGN_PASS is not set`** — You need to `source ~/.zprofile` in the current shell, or open a new terminal after editing it.

**`No runs found for build-windows.yml on main`** — You need to push a commit to `main` first, or trigger the workflow manually with `gh workflow run build-windows.yml`.

**macOS build: `ld: warning: building for macOS-11.0, but linking with dylib... built for newer version`** — This is a harmless warning. The FMWrapper.framework from Claris is compiled for a newer macOS than our minimum deployment target of 11.0.

**Docker build fails on Apple Silicon with `exec format error`** — Docker Desktop must have the Rosetta option enabled for x64 emulation (Docker Desktop → Settings → General → Use Rosetta for x86/amd64 emulation on Apple Silicon).
