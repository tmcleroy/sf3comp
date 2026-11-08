# Releasing

Versions follow `MAJOR.MINOR.PATCH`, with `-beta.N` or `-rc.N` while a version is not final. A tag is `v` and the
version (`v0.1.0-beta.3`). A release with a `-` in its version is marked as a pre-release on GitHub. Versions stay
beta until every platform's download has been played by hand.

The downloads contain code translated from the game, so CI cannot build them: a maintainer with the ROM sets builds
them and attaches them to the draft that the tag opens.

## Steps

1. **In a pull request:** set `VERSION`, and move what is under "Unreleased" in `CHANGELOG.md` to a new
   `## <version>` section. `tools/audit.sh` and `tools/ci_smoke.sh` pass, and the game's checks
   (`cps3recomp/tools/check.sh`, `games/sfiii3/check*.sh`) for what changed. Merge.
2. **Tag** the merged commit on `main` and push the tag:

   ```sh
   git switch main && git pull
   git tag -a "v$(cat VERSION)" -m "sf3comp $(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

   The `Release` workflow checks that the tag is `VERSION`, runs the audit and CI's builds, and opens a **draft**
   release whose notes are `tools/release/notes.sh`'s (the downloads, how to install, the changelog's section). It
   fails if the changelog has no such section.
3. **Build the downloads** from the tagged commit, on a Mac with Docker and your own ROM sets:

   ```sh
   git status --short            # must print nothing: build exactly what was tagged
   bash cps3recomp/tools/translate.sh path/to/sfiii3nr1.zip      # and the stage games' (docs/BUILDING.md)
   tools/release/all.sh          # audit, then macOS, Linux x86_64, Windows, then SHA256SUMS.txt
   ```

   Each script refuses to pack an archive that holds game data or a personal path, or lacks a licence
   (`tools/release/check_archive.sh`).
4. **Try each download** before anyone else does: unpack it somewhere else, start the launcher, choose the ROM set,
   play a round with a gamepad and with the keyboard. macOS on a Mac, the Linux one on a Steam Deck or Steam
   Machine, the Windows one on a Windows PC.
5. **Upload and publish:**

   ```sh
   cd build/release
   gh release upload "v$(cat ../../VERSION)" SF3comp-macOS.zip SF3comp-linux-x86_64.tar.gz \
       SF3comp-windows-x64.zip SHA256SUMS.txt
   gh release edit "v$(cat ../../VERSION)" --draft=false
   ```

6. Add an empty "Unreleased" section to `CHANGELOG.md` for what comes next.

## If something is wrong after publishing

Don't replace the files of a published release. Fix it, raise the version (`-beta.N+1`, or the patch number) and
release again; say in the changelog what was wrong.

## Source for the binaries

The GPL-3.0 asks that the source of a binary be offered with it. The tag is that source: GitHub attaches it to every
release, and each archive's `SOURCE.txt` points to it. Build only from a tagged, clean commit so that this stays
true.
