# Acceptance by hand

The fake PackageKit (`tests/fake-packagekit.py`) replays what the real daemon was recorded
doing. These steps check the real thing: before each release, and once when milestone 4 lands.
This is Administer, chosen deliberately: the Engineer alternative is a VM with a real apt
archive and a polkit agent, which is not proportionate for a single-user tool (plan §4.3).

Run the release build's binary on the host (`out/release/chandler`, or the installed one).
The app installed is GNOME's 2048, package `gnome-2048`: it is in the catalog, it is one
package, and on 26.04 it brings no others (`apt-get install -s gnome-2048` lists one `Inst`).
(`hello`, used for the recordings, has no catalog entry, so Chandler can't list it.) It
must not be installed at the start: `apt policy gnome-2048` shows "Installed: (none)".
Remove it afterwards with `sudo apt remove gnome-2048`.

Write down the date, the build hash (`chandler --version`), and the result of each step.

## 1. Install, and the row reads Installed

1. `chandler 2048` (a text search). The "2048" row shows Install.
2. Press Install. Expect "Checking…", "Starting…", then the password prompt, with "Waiting
   for authentication…" in the row.
3. Enter the password. Expect "Installing…", then **Installed**. The info bar stays empty.

## 2. The prompt keeps its window

1. `sudo apt remove gnome-2048`, then search again (or relaunch).
2. Press Install. When the password prompt is up, press Esc in Chandler's window and click
   its close button. Expect the window to **stay**, with "Answer the password prompt first".
3. Cancel the prompt. Expect "Not installed: authentication didn't complete." and the row
   back at Install. Now the window closes.

## 3. polkit's kept authorization is revoked

1. Do step 1 (install with the password).
2. `sudo apt remove gnome-2048`. In the **same** Chandler window, search again and press Install.
3. Expect **a password prompt again**. Without the revoke, polkit would let Chandler install
   for five minutes without one (plan §3.3).

## 4. No network

1. `sudo apt remove gnome-2048`. Turn the network off.
2. Press Install and enter the password. Expect "Couldn't download the package." and the row
   back at Install. Turn the network on again.

## 5. Closing once it's working

1. With the network on, press Install, enter the password, and close the window as soon as
   it says "Installing…". Expect the window to disappear and `apt policy gnome-2048` to show it
   installed a few seconds later. `pgrep -x chandler` finds nothing once it is done.

## 6. The fake against the real daemon

1. `chandler --list application/pdf` and `chandler --list text/markdown`.
2. Compare each row's state with what the scenarios in `tests/data/pk/` claim for those
   packages on this machine. A difference means the fake has drifted: record again
   (`tools/record-pk.sh`) before trusting the tests.

## 7. A package that asks a debconf question (optional)

The owner accepted that these may fail (plan §1.2 item 3); this only records what happens.
It installs Microsoft's font installer and two small dependencies.

1. `chandler ttf-mscorefonts-installer`. Press Install; the row asks to confirm ("also
   installs 2 packages"). Confirm and enter the password.
2. Write down what happens with its licence question: a dialog, a hang, or a failure, and
   what Chandler shows.
3. `sudo apt purge ttf-mscorefonts-installer && sudo apt autoremove --dry-run` (check that
   the dry run lists only `cabextract` and `libmspack0t64` before running it for real).

## 8. The installer and the shim

1. `./build install` (or the curl one-liner once a release is published). Expect four files
   listed, the shim among them; `gnome-software --version` prints `chandler <hash>`.
2. Log out and in (the menu reads `.desktop` files at login on Xfce). Chandler is in the
   menu under System, with the candle icon; it opens with an empty search. The open window
   shows the candle in the panel's task list too (the `.desktop` file's `StartupWMClass`
   matches the window; if the panel shows a generic icon, say so).
3. In Quoin, select a PDF, open "Open With…", press **Find New Applications**. Expect
   Chandler, searching `application/pdf`, above the dialog.
4. The same with a file Quoin can't type and with two types selected: Chandler opens with
   an empty search (`application/octet-stream`, plan §6).
5. `CHANDLER_UNINSTALL=1 ./install.sh` removes the four files; re-install after.
