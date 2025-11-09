# CI Monitoring Guide

This document explains how to monitor the GitHub Actions CI/CD pipeline for the GStreamer plugin.

## Overview

The GStreamer plugin has automated CI/CD configured via GitHub Actions. The workflow runs on:
- Every push to `master` or `claude/gstreamer-*` branches
- Every pull request that modifies GStreamer plugin code
- Manual triggers via workflow_dispatch

## Workflow Details

**Workflow Name:** `GStreamer Plugin CI`
**File:** `.github/workflows/gstreamer-plugin.yml`

### Jobs

1. **ubuntu-gstreamer-build** (Ubuntu 24.04)
   - Installs GStreamer 1.0 development libraries
   - Builds whisper.cpp core library
   - Builds GStreamer plugin with meson
   - Runs unit tests
   - Runs plugin discovery tests
   - Runs full test suite
   - Uploads build artifacts and test logs

2. **ubuntu-gstreamer-minimal** (Ubuntu 22.04)
   - Tests on older Ubuntu LTS version
   - Minimal dependency installation
   - Ensures backward compatibility

## Monitoring Tools

### 1. Simple Status Check (curl-based)

Use the basic script for quick status checks without installing additional tools:

```bash
./scripts/check-ci.sh [branch-name]
```

**Examples:**
```bash
# Check current branch
./scripts/check-ci.sh claude/gstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb

# Check master branch
./scripts/check-ci.sh master
```

**Features:**
- Shows last 5 workflow runs
- Color-coded status indicators (✅ success, ❌ failure, ⏳ in progress)
- Failed job details
- Summary statistics
- Exit codes: 0 (success), 1 (failure), 2 (in progress), 3 (unknown)

**Requirements:**
- curl (usually pre-installed)

### 2. Advanced Monitoring (gh CLI-based)

Use the advanced script for detailed monitoring with watch mode:

```bash
./scripts/ci/monitor-gstreamer-build.sh [options]
```

**Options:**
- `--watch`: Enable watch mode (auto-refresh)
- `--branch BRANCH`: Specify branch to monitor
- `--interval SECONDS`: Set refresh interval (default: 30s)
- `--help`: Show usage information

**Examples:**
```bash
# One-time status check
./scripts/ci/monitor-gstreamer-build.sh

# Watch mode with auto-refresh
./scripts/ci/monitor-gstreamer-build.sh --watch

# Monitor specific branch
./scripts/ci/monitor-gstreamer-build.sh --branch master --watch

# Custom refresh interval
./scripts/ci/monitor-gstreamer-build.sh --watch --interval 60
```

**Features:**
- Real-time status updates
- Detailed job information
- Automatic artifact download on completion
- Failed step highlighting
- Color-coded output

**Requirements:**
- [GitHub CLI (gh)](https://cli.github.com/)
- jq (JSON processor)
- GitHub authentication: `gh auth login`

**Installation:**
```bash
# Install GitHub CLI (Ubuntu/Debian)
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh jq

# Authenticate
gh auth login
```

## Web Interface

You can also view CI status directly on GitHub:

**Actions Dashboard:**
```
https://github.com/ifitzpat/whisper.cpp/actions
```

**Filter by Branch:**
```
https://github.com/ifitzpat/whisper.cpp/actions?query=branch%3Aclaude%2Fgstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb
```

**Specific Workflow:**
```
https://github.com/ifitzpat/whisper.cpp/actions/workflows/gstreamer-plugin.yml
```

## Artifacts

Build artifacts are uploaded for every run and include:
- `gstreamer-plugin-ubuntu`: Compiled plugin (.so files)
- `test-logs-ubuntu`: Test output and meson logs

**Download artifacts:**
1. Via web: Go to the workflow run → "Artifacts" section
2. Via CLI: Use the monitor script with download prompt
3. Via gh CLI:
   ```bash
   gh run download <run-id> -R ifitzpat/whisper.cpp
   ```

## Troubleshooting

### CI Failing

1. **Check the logs:**
   ```bash
   ./scripts/check-ci.sh
   ```

2. **View failed jobs:**
   ```bash
   ./scripts/ci/monitor-gstreamer-build.sh
   ```

3. **Download artifacts for local inspection:**
   ```bash
   gh run list -R ifitzpat/whisper.cpp -w "GStreamer Plugin CI" -L 1
   gh run download <run-id> -R ifitzpat/whisper.cpp
   ```

### Authentication Issues

If you see "Not authenticated" errors:
```bash
gh auth login
# Follow the prompts
```

### Rate Limiting

The GitHub API has rate limits. If you hit them:
- Use authenticated requests (automatic with gh CLI)
- Increase `--interval` in watch mode
- Use the web interface

## Local Testing

Before pushing, test locally to catch issues early:

```bash
cd gstreamer
./build-standalone.sh
./test-plugin.sh
```

## Status Badge

Add this badge to your README to show CI status:

```markdown
[![GStreamer Plugin CI](https://github.com/ifitzpat/whisper.cpp/actions/workflows/gstreamer-plugin.yml/badge.svg?branch=claude/gstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb)](https://github.com/ifitzpat/whisper.cpp/actions/workflows/gstreamer-plugin.yml)
```

## Integration with Development Workflow

### TDD Cycle with CI

1. **Write failing test** (Red)
   ```bash
   cd gstreamer
   # Edit test-plugin.sh or tests/test_plugin.c
   ./test-plugin.sh  # Should fail
   ```

2. **Implement feature** (Green)
   ```bash
   # Edit src/gstwhispertranscribe.c
   ./test-plugin.sh  # Should pass
   ```

3. **Commit and push**
   ```bash
   git add .
   git commit -m "Add feature X"
   git push -u origin claude/gstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb
   ```

4. **Monitor CI**
   ```bash
   ./scripts/ci/monitor-gstreamer-build.sh --watch
   ```

5. **Refactor** if tests pass

### Pre-push Checklist

- [ ] Local tests pass: `./test-plugin.sh`
- [ ] Code compiles: `meson compile -C builddir`
- [ ] No warnings in build output
- [ ] Updated documentation if needed

## Advanced Usage

### Watch Multiple Branches

```bash
# Terminal 1
./scripts/ci/monitor-gstreamer-build.sh --branch master --watch

# Terminal 2
./scripts/ci/monitor-gstreamer-build.sh --branch claude/gstreamer-whisper-plan-011CUxys8upnQuZaQTXuTAcb --watch
```

### Filter Workflow Runs

```bash
# Show only successful runs
gh run list -R ifitzpat/whisper.cpp -w "GStreamer Plugin CI" -s success -L 10

# Show only failed runs
gh run list -R ifitzpat/whisper.cpp -w "GStreamer Plugin CI" -s failure -L 10

# Show runs for specific branch
gh run list -R ifitzpat/whisper.cpp -w "GStreamer Plugin CI" -b master -L 10
```

### Cancel Running Workflow

```bash
# Get run ID
gh run list -R ifitzpat/whisper.cpp -w "GStreamer Plugin CI" -L 1

# Cancel run
gh run cancel <run-id> -R ifitzpat/whisper.cpp
```

### Re-run Failed Jobs

```bash
gh run rerun <run-id> -R ifitzpat/whisper.cpp --failed
```

## Notifications

Set up notifications for CI status:

1. **GitHub Web:** Settings → Notifications → Actions
2. **GitHub Mobile App:** Get push notifications
3. **Email:** Configure in GitHub settings
4. **Slack/Discord:** Use GitHub webhooks

## Performance

**Typical CI Run Times:**
- ubuntu-gstreamer-build: ~5-8 minutes
- ubuntu-gstreamer-minimal: ~3-5 minutes

**Factors affecting speed:**
- whisper.cpp core library build (~2-3 min)
- Dependency installation (~1-2 min)
- Plugin build (~1 min)
- Tests (~1-2 min)

## Contributing

When adding new tests or build steps:

1. Update `.github/workflows/gstreamer-plugin.yml`
2. Test locally first
3. Document changes in this file
4. Ensure artifacts are uploaded for debugging

## Support

- CI Issues: Check [GitHub Actions Status](https://www.githubstatus.com/)
- Build Issues: See `gstreamer/QUICKSTART.md`
- Test Issues: See `gstreamer/TDD_STATUS.md`
