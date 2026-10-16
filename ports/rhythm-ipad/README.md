# Experimental rhythm-game iPad host

This independent target preserves the upstream host and its Android/iOS targets.
It starts from the game-specific Android host adaptations and reuses the signed
Guest.framework loader, CoreAudio mixer, and NVN replay through MoltenVK 1.4.2.
The upstream fork base is `dodola/d2r-mobile` commit `642480b`.

Status: work in progress. Compilation and device execution must be reported
separately. No iPad gameplay success is claimed by a successful shell build.

Run on a Mac with Xcode: `python3 dm/ipad/build.py` from this directory.
GitHub Actions builds only the shell. Original modules, RomFS, shader data,
system fonts, keys and saves must stay local and are not included in this tree.
The local packer will add the owner's game content after download; the whole
package then requires Apple signing before device installation.

The source snapshot is intentionally isolated while the portable platform
boundary is being established. Existing upstream targets are not overwritten.
The MIT host license and third-party notices accompany the build.
