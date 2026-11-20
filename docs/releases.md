# CI, releases and GitHub setup

The default branch is `main`. The workflow in `.github/workflows/ci.yml`
runs host regressions and dashboard fixtures before building with
`espressif/idf:v5.5.4`. It uses the checked-in configuration and `build.sh`
SDK patches, then merges the factory image and uploads the distribution.
Pull requests use `pull_request` with read-only repository permissions.
Only the tag release job receives permission to publish releases.

## Initial publication

Create an empty GitHub repository (without a generated README or license),
then run these commands in the prepared repository, substituting the real URL:

```sh
git remote add origin https://github.com/OWNER/ESPAirspy.git
git push -u origin main
```

In GitHub settings:

- Set `main` as the default branch and enable GitHub Actions.
- Protect `main` with pull requests and the required checks `host-tests` and
  `firmware` after their first run. Restrict who can push release tags.
- Allow the workflow's release job to use `contents: write`; organization policy
  can restrict this even when requested by the workflow.
- Enable private vulnerability reporting if available (see `SECURITY.md`).

No personal access token or additional repository secret is needed. Actions
uses the repository's automatically provided `GITHUB_TOKEN` for releases.
Builds are not published until this repository is pushed to GitHub.

## Publish a release

For a firmware change, update `PROJECT_VER` in `CMakeLists.txt`,
`CHANGELOG.md` and the in-app changelog together. Merge the pull request to
`main` and wait for CI. Tag that reviewed commit with the matching version:

```sh
git switch main
git pull --ff-only
git tag -a v0.4.0 -m "ESPAirspy 0.4.0"
git push origin v0.4.0
```

Use the actual version instead of `0.4.0` for later releases. A mismatched tag
fails before building. The tag workflow repeats tests and builds, then creates
a GitHub Release containing the OTA app, merged factory image, installation
instructions, build identity, changelog, license notices and checksums.
Do not move an existing release tag; fix the problem in a new version.
After a publication-only failure, inspect for a partial release before retrying.

For a local equivalent, activate ESP-IDF and run
`./scripts/package-firmware.sh` from a committed checkout. Output goes to a
new `dist/` directory; move an existing `dist/` aside before packaging again.
Firmware version 0.4.0 is retained for this repository/CI preparation because
no firmware behavior changed.
