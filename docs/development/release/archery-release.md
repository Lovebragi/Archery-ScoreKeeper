<p align="right">
  <a href="archery-release.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Archery scorekeeper release management

How the archery scorekeeper is versioned, tagged, released on GitHub and
published to the community. The application lives in this repository; its
history is tracked by [`docs/applications/archery-scorekeeper-changelog.md`](../../applications/archery-scorekeeper-changelog.md).

## Version source

`main/archery_version.h` is the single source of truth:

```c
#define ARCHERY_FIRMWARE_VERSION "1.1.0"
#define ARCHERY_FIRMWARE_CHANNEL "<channel>"
```

- Use semantic versions (`MAJOR.MINOR.PATCH`) for the firmware.
- The channel is a short label (`release` for public builds, a distinct label
  for experiments); it appears in the boot log so a flashed device is identifiable.
- The application changelog must contain a section `## <version>` before a tag is
  created.

## Artefact map

| Version | Git tag | Firmware SHA-256 | GitHub Release | Community revision |
| --- | --- | --- | --- | --- |
| 1.1.0 | `v1.1.0` | `17b8dcbf…57be6d` | v1.1.0 | 3713 (pending) |
| 1.0.0 | `v1.0.0` | `ed68c310…f8861153` | v1.0.0 | 3701 (approved) |

Community revision numbers are assigned by the platform and cannot be chosen;
record them here after a submission. A revision is only public once its review
status is `approved`.

## Commands

The helper only touches local files or the GitHub API when the matching
subcommand is used; nothing is pushed implicitly.

```bash
python tools/release.py status        # version, HEAD, tags, firmware hash
python tools/release.py check         # version/changelog/firmware consistency
python tools/release.py bump 1.2.0 --notes-file notes.md
python tools/release.py tag --push    # annotated tag on HEAD, optional push
python tools/release.py github        # create/update Release, upload firmware
```

`bump` updates `main/archery_version.h` and inserts a changelog section above the
previous one. It does not commit, build or tag; review the diff first.

`github` reads the token from `GH_TOKEN` and the repository from
`git remote get-url origin`. It creates the release for `v<version>` if it does
not exist, then uploads `build/FoloToy-AI-Passport-full.bin`, replacing an asset
with the same name. Set `HTTPS_PROXY` when GitHub is not directly reachable.

## Release checklist

1. `python tools/release.py check` passes and the working tree is clean.
2. `python tools/release.py bump <version> --notes-file <file>`; review the
   changelog and version header.
3. `./tools/validate.sh --static` and `./tools/validate.sh --firmware`.
4. `python tools/release.py tag --push`.
5. `python tools/release.py github` with `GH_TOKEN` set.
6. Verify the uploaded asset by downloading it and comparing the SHA-256.
7. Publish to the community with the publisher skill and a one-time grant; see
   [`publish-to-community.md`](publish-to-community.md). Update the artefact map
   above with the new revision.

## Secrets and authorization

- Never commit tokens, grants or credentials. Put the GitHub token in the
  `GH_TOKEN` environment variable only; the community publisher keeps its
  credentials outside the repository.
- Community updates need a fresh one-time authorization code issued for the
  project; the tooling cannot bypass it.
- Do not rewrite or move an existing tag that has a published release; bump the
  version instead.
