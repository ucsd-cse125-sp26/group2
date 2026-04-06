# IDE Integration & Git Tag Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CLion, VS Code, and Visual Studio 2022 automatically use the repo's CMakePresets.json profiles on first open; fix git tag fetch in all setup scripts and README.

**Architecture:** Punch `.idea/workspace.xml` and the new `.vscode/` directory through the existing gitignore via selective negation patterns. Commit a minimal `workspace.xml` skeleton (CMakePresetLoader + CMakeSettings only — presets on, CLion default off). Add VS Code config (4 files) and a `launch.vs.json` for VS 2022. Append the one-line git tag fix to all 4 setup scripts and update README.

**Tech Stack:** CMakePresets.json (already exists), CLion `workspace.xml` XML, VS Code CMake Tools extension JSON config, Visual Studio 2022 `launch.vs.json`.

---

### Task 1: Fix .gitignore to allow workspace.xml and .vscode/

**Files:**
- Modify: `.gitignore`

The current `.gitignore` has `.idea/` and `.vscode/` as blanket rules, preventing us from committing
the config files we need. We use selective negation patterns instead.

`.idea/*` ignores everything inside `.idea/` but allows `!.idea/workspace.xml` to punch through.
`.vscode/` is replaced by nothing (removed entirely) — all `.vscode/` files are project config, not user state.

- [ ] **Replace the IDE block in .gitignore**

Open `.gitignore`. Find the `# IDE / editor` block (lines 13–20 currently):
```
# IDE / editor
.vscode/
.idea/
*.user
*.suo
*.sln.docview
.vs/
*.swp
*.swo
*~
```

Replace it with:
```
# IDE / editor
.idea/*
!.idea/workspace.xml
*.user
*.suo
*.sln.docview
.vs/
*.swp
*.swo
*~
```

Key changes:
- `.idea/` → `.idea/*` + `!.idea/workspace.xml`  (tracks only workspace.xml)
- `.vscode/` removed entirely  (all .vscode/ files will be committed)

- [ ] **Also add cmake-build-debug/ to the Build output block**

In the `# Build output` block at the top of `.gitignore`, add `cmake-build-debug/`:
```
# Build output
build/
out/
cmake-build-debug/
.cache/
```

- [ ] **Verify changes look right**

```bash
git diff .gitignore
```

Expected: see the `.idea/` → `.idea/*` + negation change, `.vscode/` removed, `cmake-build-debug/` added.

- [ ] **Stage — do not commit yet (more files coming)**

```bash
git add .gitignore
```

---

### Task 2: Create .gitattributes

**Files:**
- Create: `.gitattributes`

`.gitattributes` marks `workspace.xml` as linguist-generated so GitHub collapses it in PR diffs
(reduces noise when CLion appends run configs). Also sets consistent line endings for shell scripts.

- [ ] **Create .gitattributes**

```
# Tell GitHub this file is auto-managed — collapse it in PR diffs
.idea/workspace.xml linguist-generated=true

# Consistent line endings
*.sh    text eol=lf
*.ps1   text eol=crlf
*.cpp   text eol=lf
*.hpp   text eol=lf
*.glsl  text eol=lf
*.vert  text eol=lf
*.frag  text eol=lf
```

- [ ] **Stage**

```bash
git add .gitattributes
```

---

### Task 3: Rewrite workspace.xml (CLion — minimal preset skeleton)

**Files:**
- Create: `.idea/workspace.xml`  (currently untracked/local-only; this becomes the committed version)

Strip workspace.xml to the two components CLion needs to auto-select preset profiles.
Everything else (run configs, window layout, recent files, VCS state) is user-specific and must
not be in this file — CLion will append its own data locally, but that stays gitignored since only
`workspace.xml` is punched through and CLion-written additions go to `workspace.xml` too... which
means CLion WILL dirty this file. That's expected and documented in the README. Teammates should
not commit workspace.xml changes unless they specifically update CMakeSettings.

The four presets enabled: `debug` (Linux/macOS), `debug-win` (Windows), `release`, `relwithdebinfo`.
The CLion auto-generated `"Debug"` profile (points at cmake-build-debug/) is absent — not disabling
it, just not listing it. CLion shows only what's in this list when workspace.xml has CMakeSettings.

- [ ] **Write .idea/workspace.xml**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <!--
    Minimal shared skeleton — only CMake preset wiring lives here.
    CLion will append run configs and UI state locally; do NOT commit those changes.
    To update preset config: edit only the <component name="CMakeSettings"> block.
  -->
  <component name="CMakePresetLoader"><![CDATA[{
  "useNewFormat": true
}]]></component>
  <component name="CMakeSettings">
    <configurations>
      <configuration PROFILE_NAME="debug"
                     PROFILE_DISPLAY_NAME="Debug (Linux/macOS — ASan + UBSan)"
                     ENABLED="true" FROM_PRESET="true"
                     GENERATION_DIR="$PROJECT_DIR$/build/debug" />
      <configuration PROFILE_NAME="debug-win"
                     PROFILE_DISPLAY_NAME="Debug (Windows — ASan via MSVC)"
                     ENABLED="true" FROM_PRESET="true"
                     GENERATION_DIR="$PROJECT_DIR$/build/debug-win" />
      <configuration PROFILE_NAME="release"
                     PROFILE_DISPLAY_NAME="Release"
                     ENABLED="true" FROM_PRESET="true"
                     GENERATION_DIR="$PROJECT_DIR$/build/release" />
      <configuration PROFILE_NAME="relwithdebinfo"
                     PROFILE_DISPLAY_NAME="RelWithDebInfo"
                     ENABLED="true" FROM_PRESET="true"
                     GENERATION_DIR="$PROJECT_DIR$/build/relwithdebinfo" />
    </configurations>
  </component>
</project>
```

- [ ] **Verify CLion can see it**

```bash
cat .idea/workspace.xml   # should show the minimal XML above
git status                # .idea/workspace.xml should appear as "new file" (was untracked)
```

- [ ] **Stage**

```bash
git add .idea/workspace.xml
```

---

### Task 4: Create VS Code config (.vscode/)

**Files:**
- Create: `.vscode/extensions.json`
- Create: `.vscode/settings.json`
- Create: `.vscode/tasks.json`
- Create: `.vscode/launch.json`

`cmake.useCMakePresets: "always"` is the key setting — it forces CMake Tools to read
CMakePresets.json instead of its own profiles. After that, everything (configure, build, launch)
flows through the preset the user picks in the status bar.

`C_Cpp.intelliSenseEngine: "disabled"` prevents the default cpptools IntelliSense from conflicting
with clangd. The cpptools debugger still works independently of IntelliSense.

The `compile_commands.json` symlink already exists in the repo root pointing at cmake-build-debug/,
so clangd finds it immediately without extra config.

- [ ] **Create .vscode/extensions.json**

```json
{
  "recommendations": [
    "ms-vscode.cmake-tools",
    "ms-vscode.cpptools-extension-pack",
    "llvm-vs-code-extensions.vscode-clangd"
  ]
}
```

- [ ] **Create .vscode/settings.json**

```json
{
  "cmake.useCMakePresets": "always",
  "cmake.sourceDirectory": "${workspaceFolder}",
  "C_Cpp.intelliSenseEngine": "disabled",
  "clangd.arguments": [
    "--background-index",
    "--clang-tidy",
    "--completion-style=detailed",
    "--function-arg-placeholders=false"
  ],
  "files.associations": {
    "*.vert": "glsl",
    "*.frag": "glsl"
  },
  "editor.formatOnSave": false
}
```

- [ ] **Create .vscode/tasks.json**

The `cmake` task type is provided by the CMake Tools extension and uses the currently-selected
preset automatically — no hardcoded preset name needed.

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CMake: Build",
      "type": "cmake",
      "command": "build",
      "group": {
        "kind": "build",
        "isDefault": true
      },
      "problemMatcher": []
    },
    {
      "label": "CMake: Configure",
      "type": "cmake",
      "command": "configure",
      "group": "build",
      "problemMatcher": []
    }
  ]
}
```

- [ ] **Create .vscode/launch.json**

`${command:cmake.launchTargetPath}` is a variable provided by CMake Tools that resolves to the
currently-selected target's output binary — platform-correct automatically (e.g. build/debug/group2
on Linux, build\debug-win\group2.exe on Windows). `preLaunchTask` triggers the default build.

The Linux config adds the LSan suppression env var to silence SDL3 false-positive leak reports.
The Windows config uses `cppvsdbg` (VS debugger, available when cpptools is installed on Windows).

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Launch group2 (Linux / macOS)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${command:cmake.launchTargetPath}",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [
        {
          "name": "LSAN_OPTIONS",
          "value": "suppressions=${workspaceFolder}/sanitizers/lsan.supp"
        }
      ],
      "MIMode": "lldb",
      "externalConsole": false,
      "preLaunchTask": "CMake: Build"
    },
    {
      "name": "Launch group2 (Windows)",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${command:cmake.launchTargetPath}",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "preLaunchTask": "CMake: Build"
    }
  ]
}
```

- [ ] **Stage all .vscode files**

```bash
git add .vscode/extensions.json .vscode/settings.json .vscode/tasks.json .vscode/launch.json
```

---

### Task 5: Create launch.vs.json for Visual Studio 2022

**Files:**
- Create: `launch.vs.json`

VS 2022 reads `CMakePresets.json` automatically on "Open Folder" and shows preset profiles in the
configuration dropdown. `launch.vs.json` in the repo root adds the explicit debug launch entry so
F5 targets `group2` with the correct working directory.

`projectTarget` must match what VS 2022 shows in the "Select Startup Item" dropdown for the CMake
target. The format is typically `"<target-name> (<relative-exe-path>)"`. If VS 2022 shows a
different string, update this field to match — the rest of the config is correct.

- [ ] **Create launch.vs.json**

```json
{
  "version": "0.2.1",
  "defaults": {},
  "configurations": [
    {
      "type": "default",
      "project": "CMakeLists.txt",
      "projectTarget": "group2.exe",
      "name": "group2 (debug-win)",
      "currentDir": "${workspaceRoot}",
      "args": []
    }
  ]
}
```

- [ ] **Stage**

```bash
git add launch.vs.json
```

---

### Task 6: Add git tag fix to all setup scripts

**Files:**
- Modify: `scripts/setup-linux.sh`
- Modify: `scripts/setup-archlinux.sh`
- Modify: `scripts/setup-macos.sh`
- Modify: `scripts/setup-windows.ps1`

Each script gets one line added just before its final `echo "Build commands:"` block.
The line configures git to force-update tags on every fetch/pull — prevents the
`[rejected] latest -> latest (would clobber existing tag)` error caused by the repo's
rolling `latest` release tag being force-pushed by CI.

- [ ] **Add to scripts/setup-linux.sh — before the final echo block**

Find the block:
```bash
echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
```

Insert before it:
```bash
echo "==> Configuring git for this repository..."
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"
```

Full result of that section:
```bash
echo "==> Configuring git for this repository..."
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"

echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
echo "  cmake --preset debug   && cmake --build --preset debug    # debug + ASan/UBSan"
echo "  cmake --preset release && cmake --build --preset release  # optimised"
```

- [ ] **Add to scripts/setup-archlinux.sh — same pattern**

Find:
```bash
echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
```

Insert before it:
```bash
echo "==> Configuring git for this repository..."
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"
```

- [ ] **Add to scripts/setup-macos.sh — same pattern**

Find:
```bash
echo ""
echo "==> All prerequisites installed."
echo ""
echo "Build commands:"
```

Insert before it:
```bash
echo "==> Configuring git for this repository..."
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"
```

- [ ] **Add to scripts/setup-windows.ps1 — before final Write-Host block**

Find:
```powershell
Write-Host ""
Write-Host "==> All prerequisites installed." -ForegroundColor Green
Write-Host ""
Write-Host "Build commands (run inside 'Developer PowerShell for VS 2022'):" -ForegroundColor Yellow
```

Insert before it:
```powershell
Write-Host "==> Configuring git for this repository..." -ForegroundColor Cyan
git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"
```

- [ ] **Stage all four scripts**

```bash
git add scripts/setup-linux.sh scripts/setup-archlinux.sh scripts/setup-macos.sh scripts/setup-windows.ps1
```

---

### Task 7: Update README.md

**Files:**
- Modify: `README.md`

Two additions:
1. A callout after the Prerequisites heading explaining the git tag config is handled by setup scripts (so people who cloned before running a script know what to do).
2. A new "## IDE setup" section after the Building section, covering CLion, VS Code, and VS 2022.

- [ ] **Add git tag callout to the Prerequisites section**

Find the line (after the Prerequisites heading, line ~38):
```markdown
## Prerequisites
```

After the line `Both scripts install: ...` paragraph for Linux, and before the macOS heading, add
a note at the END of the Prerequisites section (after the Windows block, before the `---`):

Find:
```markdown
Visual Studio 2022 is a manual prerequisite (free Community edition). The script installs `cmake`, `ninja`, and LLVM via `winget`.

---

## Building
```

Replace with:
```markdown
Visual Studio 2022 is a manual prerequisite (free Community edition). The script installs `cmake`, `ninja`, and LLVM via `winget`.

> **Note — git tag fetch:** The setup scripts also run `git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"` which prevents `git pull` from failing with "would clobber existing tag". If you cloned before running a setup script, run that command once manually.

---

## Building
```

- [ ] **Add IDE setup section after the Building section**

Find:
```markdown
---

## Running
```

Insert before it:
```markdown
---

## IDE setup

### CLion
Open the **repo root folder** in CLion. It reads `CMakePresets.json` automatically. The preset
profiles (`debug`, `debug-win`, `release`, `relwithdebinfo`) are pre-enabled via `.idea/workspace.xml`
committed in this repo — they should appear already checked in
**Settings › Build, Execution, Deployment › CMake**.

If CLion has added its own "Debug" profile (pointing at `cmake-build-debug/`), delete it there and
keep only the preset-based ones.

> **Note:** CLion will modify `workspace.xml` locally as you work (adding run configs, UI state).
> Do **not** commit those changes — only commit deliberate edits to the CMakeSettings block.

### VS Code
1. Install the recommended extensions when prompted (`.vscode/extensions.json` is committed).
   Key extension: **CMake Tools** (`ms-vscode.cmake-tools`).
2. CMake Tools detects `CMakePresets.json` automatically (`cmake.useCMakePresets: "always"` is set).
3. Select a preset from the status bar (`debug` on Linux/macOS, `debug-win` on Windows).
4. **Build:** `Ctrl+Shift+B` or the ▶ build button in the status bar.
5. **Debug:** `F5` → pick "Launch group2 (Linux / macOS)" or "Launch group2 (Windows)".

### Visual Studio 2022
Use **File › Open › Folder** (not *Open › Project/Solution*) to open the repo root.
VS 2022 reads `CMakePresets.json` natively — the `debug-win` and `release` presets appear in the
configuration dropdown at the top of the window.

1. Select `debug-win` from the configuration dropdown.
2. **Build:** `Ctrl+Shift+B`.
3. **Run/Debug:** `F5` — VS 2022 auto-detects the `group2.exe` CMake target.

```

- [ ] **Stage README**

```bash
git add README.md
```

---

### Task 8: Commit everything

- [ ] **Verify staged files**

```bash
git status
```

Expected staged files:
```
.gitignore
.gitattributes
.idea/workspace.xml
.vscode/extensions.json
.vscode/settings.json
.vscode/tasks.json
.vscode/launch.json
launch.vs.json
scripts/setup-linux.sh
scripts/setup-archlinux.sh
scripts/setup-macos.sh
scripts/setup-windows.ps1
README.md
docs/superpowers/plans/2026-04-06-ide-integration.md
```

- [ ] **Commit**

```bash
git commit -m "$(cat <<'EOF'
chore: add IDE preset config for CLion/VS Code/VS2022, fix git tag fetch

- .idea/workspace.xml: minimal skeleton enabling all CMakePresets profiles;
  punched through gitignore so teammates get correct CLion config on clone
- .vscode/: CMake Tools preset config, clangd settings, build tasks,
  cross-platform launch configs (Linux/macOS lldb + Windows cppvsdbg)
- launch.vs.json: VS 2022 Open Folder debug entry pointing at group2.exe
- All setup scripts: git config force-tag-fetch to prevent pull failures
- README: IDE setup section + git tag fix callout
- .gitattributes: workspace.xml as linguist-generated, LF/CRLF line endings
- .gitignore: cmake-build-debug/, .idea/* negation, remove .vscode/ blanket rule
EOF
)"
```

Expected: commit succeeds, pre-commit hook runs clang-format on staged .cpp/.hpp (none staged here, so hook passes immediately).
