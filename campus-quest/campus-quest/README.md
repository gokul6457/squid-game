# Campus Quest

Campus Quest is a browser game with independent player accounts and a staff results portal. The game is now a beginner debugging challenge: players find 20 short Python, C and Java snippets, identify the 10 valid ones, and submit them. It uses Node.js and a local SQLite database; it does not need a hosted service for a single-server LAN event. By default it runs at `http://localhost:4000`; LAN players use the address printed by `start.bat`, on port 4000.

## Run on Windows

1. Install Node.js 22.13 or newer.
2. Before starting, set a unique `adminPassword` in `config.json` or set the `ADMIN_PASSWORD` environment variable. The server refuses to start with a missing password or the old `admin@123` default.
3. Extract this ZIP, open the extracted `campus-quest` folder, and double-click `start.bat` (or run `node server.js`). Start the server only once; if port 4000 is already in use by Campus Quest, leave that server window open and use its URL instead of launching another copy. To run a second copy on a different port, use `set PORT=4001 && node server.js` in Command Prompt and share the `:4001` URL.
4. Keep the server window running. Players on the same network open the `On <adapter name>` address shown there. Use the address for the Ethernet adapter connected to the lab network (not `localhost`, a VPN, or an unrelated Wi-Fi adapter). Staff open that address with `/admin` added; the link is not shown on the player page.
5. If other devices cannot connect, allow TCP port 4000 through Windows Firewall. In an Administrator PowerShell window on the server computer, run:

   ```powershell
   New-NetFirewallRule -DisplayName "Campus Quest LAN" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4000 -Profile Any -RemoteAddress LocalSubnet
   ```

   This rule permits connections to the game port only from the computer's local subnet. The server already listens on all interfaces; do not run a second server on each player device.

## Connect lab devices

One computer hosts the server and database; all player devices visit that computer's Ethernet IPv4 address in a browser. Connect the host and all players to the same lab LAN/subnet (wired Ethernet is best for a large group), keep the host computer awake with `start.bat` running, and use the exact `On <adapter name>` URL printed in its window. For example, if the host prints `On Ethernet: http://172.50.50.116:4000`, open that URL on each player device. Do not use the example address if the server prints a different one.

Test from one other device before sharing the URL with all 50 players. On another Windows PC, run `Test-NetConnection <server-ip> -Port 4000` in PowerShell; `TcpTestSucceeded: True` means the network connection is open. Then open `http://<server-ip>:4000` in its browser. If the test fails, confirm the firewall rule was created on the host, the server window is still running, and the lab router/switch has not enabled client isolation or separated the devices into different VLANs. Ask the network administrator if those settings are managed by the school.

For the current computer, `172.50.50.116` is its Ethernet address and the Ethernet network is marked Public by Windows. If other devices still cannot connect after adding the rule, check that they are on the same lab subnet and that the school's network policy permits device-to-device connections. The admin portal is at `http://<server-ip>:4000/admin`; share it only with staff.

## Player accounts

Players first choose **Sign in** or **Register**. Registration asks for team name, email, and password; afterward, the player returns to Sign in and logs in to start the game. Passwords are stored as salted PBKDF2 hashes. Each account gets one player registration and one attempt by default. Leaving full screen, switching tabs, pressing Escape, or changing windows pauses the game and shows a 10-second return warning before a forfeit is saved. Refreshing or closing the page shows the browser's leave-page confirmation; if the player leaves, the attempt is forfeited. Staff can allow one retry for a forfeited, timed-out, or disconnected game from the Registrations tab; the player must sign in again after staff allows it. The server grants one active game session per account across tabs and computers and expires an unexpectedly disconnected session after 45 seconds without a heartbeat.

The server calculates results from the snippet tokens issued for that game and ignores browser-supplied score counts. This prevents a direct API request from choosing its own score, but browser games cannot provide strong anti-cheat guarantees against a determined user who modifies the client.

After player registration, the address changes to a readable team path (for example, `/google` or `/google-team`) and the browser tab title shows the team. Share the same server address with everyone; the path is a friendly label, not a separate server or security boundary. Game sessions and results still go through the same backend and database, where the account lock prevents the same account from playing simultaneously on another tab or computer.

After joining, the address changes to a readable team path (for example, `/google`). The path is a friendly label, not a separate server or security boundary. The sign-in token is kept in that browser's local storage. For a shared computer, use a separate browser profile or clear the previous player's session before the next person signs in. Accounts and scores are stored on the server.

## Data and backups

The server uses Node.js's built-in SQLite support; no separate SQL server is required. It creates `data/campus-quest.sqlite` on first run and writes accounts, results, game challenges, and login sessions there. In the extracted project, the full path is `<project folder>\data\campus-quest.sqlite`. SQLite uses write-ahead logging and indexes the account and team lookups. Back up the database by copying the `data` folder while the server is stopped. To reset an event, stop the server and remove `data/campus-quest.sqlite` and its `-wal`/`-shm` files. Existing `data/db.json` files are imported on first launch when the SQLite database is empty.

SQLite is suitable for a single app server and a small LAN event. For multiple app servers, move storage to PostgreSQL and use HTTPS with a production process manager; this package is not a production high-availability setup. 100 simultaneous players can still be limited by the host computer, LAN, and each player's device. The game renders locally in each browser, so the server mainly handles sign-in and small result requests.

## Admin portal

The `/admin` page uses the password in `config.json` (or `ADMIN_PASSWORD` environment variable). It shows registrations, results, and team rankings. On the Registrations tab, staff can allow one retry after a forfeited or timed-out attempt, reset a player's password, or remove a player; removing deletes that player's registration, saved result, and account when no other player record uses it. Keep the portal private to staff.

## Tests

Run `npm test` with Node.js 22.13 or newer. Tests use a temporary SQLite database and do not change the event database.

## Files

- `server.js`: HTTP server, account authentication, game API, and admin API.
- `public/`: game website and browser assets, including the compiled WebAssembly game.
- `game-src/`: game source and web build scripts.
- `data/`: SQLite runtime database, created on first launch (not included in the ZIP).
