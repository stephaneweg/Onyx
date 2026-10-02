# Mail for Onyx — study, first mock-ups

> **Status (2026-10-02): mock-ups, to validate.** Priority 1 of the end-user apps roadmap (docs/HANDOFF.md): a mail
> client "as user-friendly as possible". The user (2026-10-02): it must connect to **Gmail, Outlook, IMAP and
> POP3 / SMTP**; Gmail with an **app password**, Outlook with Microsoft's sign-in **by a code** (an "Onyx Mail"
> application registered by the user at Microsoft: below). **Contacts** = a Cardfile form (`.card`), opened in
> Cardfile too. Proposed name: **Mail** (app folder `mail`).

The mock-ups are made by `python3 tools/screenshot/mockup_mail.py` → `docs/mail/mockups/*.png` (1024 × 768, the real
desktop behind; the drawing helpers are `mockup_archiver.py`'s; the people and their messages are made up; the
providers' marks are drawn in their colours, not their logos).

| | |
|---|---|
| ![](mockups/mail-inbox.png) | **The inbox.** At the left the **unified inbox** (all the accounts), Starred, then **each account** with its folders (Inbox, Sent, Drafts, Archive, Junk, Trash and its own), the unread counted; at the bottom **Contacts** and the settings. In the middle the **conversations**, by day (Today, Yesterday, This week): who, the subject, the start of the text, the time, how many messages, a paper clip, a star; a coloured stripe tells the account; All / Unread. At the right the **message**: who, to whom, when, Reply / Reply all / Forward, the text, the **attachments** (open in the right app — a PDF in the PDF Viewer, a picture in the Image Viewer — or save), a **quick reply** at the bottom. The toolbar: **New message**, Reply, Reply all, Forward, Archive, Delete, Junk, Star, the **search** (all the accounts: who, subject, text), check now. |
| ![](mockups/mail-thread.png) | **A conversation**: its messages folded (who, the first line, the date), the last one open; its quoted part dimmed. |
| ![](mockups/mail-compose.png) | **Writing**: From (the account), To / Cc / Bcc with the people as chips, **completed from the contacts** as one types (and from the addresses already written to); the subject; bold, italic, underline, a list, a link, a picture, an attachment (or files dropped from the File Viewer); the draft kept every few seconds (IMAP: in the account's Drafts); **Send** (Ctrl+Enter). A message is sent as text **and** HTML (simple). |
| ![](mockups/mail-wizard.png) | **Adding an account**: the address typed, the provider **recognised** (Gmail, Outlook.com / Hotmail / Live, iCloud, Yahoo, GMX, Proximus, Telenet, Orange, Free…: the servers filled in), or the kind chosen: Gmail, Outlook, iCloud, Yahoo, another IMAP account, a POP3 account. |
| ![](mockups/mail-gmail.png) | **Gmail**: the **app password** (Google refuses the usual password in mail apps since 2025): two-step verification on, the password made at `myaccount.google.com/apppasswords` (opened in Jet Browser), its 16 letters typed. The servers filled in (`imap.gmail.com:993`, `smtp.gmail.com:465`). |
| ![](mockups/mail-outlook.png) | **Outlook.com / Hotmail**: Microsoft refuses passwords (since September 2024) — **OAuth 2**, by the **device code**: Mail shows a code; on a phone or a PC, `microsoft.com/link` (or the QR code), the code typed, the Microsoft account's sign-in, "Onyx Mail" allowed; Mail waits and goes on by itself. Then IMAP `outlook.office365.com:993` and SMTP `smtp-mail.outlook.com:587` with **XOAUTH2**; the token renewed by itself (the refresh token). |
| ![](mockups/mail-manual.png) | **By hand**: IMAP or **POP3** (server, port, security SSL/TLS or STARTTLS, user name), POP3's options (leave the messages on the server, delete them there after n days; the folders are then the card's own), SMTP (server, port, security; the same name and password or others). The **Check** step tries both and says what is wrong in words (server not found, certificate, password refused…). |
| ![](mockups/mail-contacts.png) | **Contacts**: a **Cardfile form** — `SD:/Documents/Contacts.card` (name, e-mails, phones, company, address, birthday, notes) —, listed A to Z with their initials, the card shown as Cardfile shows it; **Write**, **Open in Cardfile**; "Add the sender to the contacts" from a message. Cardfile opens the same file. |

## How it would be built

| Need | Onyx today | To add |
|---|---|---|
| TLS | mbedTLS 3.6.3 (`third_party`, Courier's `tls/onyx_tls.hpp`, Jet, `pkg`) over the kapi sockets; `SD:/res/ca-bundle` | — |
| IMAP4rev1 / IMAP4rev2 | — | our own client (`user/mail/imap.h`, MIT): LOGIN, **AUTHENTICATE XOAUTH2**, LIST, SELECT / EXAMINE, UID FETCH (ENVELOPE, FLAGS, BODYSTRUCTURE, BODY.PEEK[parts]), UID SEARCH, UID STORE (\Seen \Flagged \Deleted), UID MOVE / COPY + EXPUNGE, APPEND (Sent, Drafts), IDLE (new mail at once), CONDSTORE when there; Gmail's labels as folders. |
| POP3 | — | `pop3.h`: USER / PASS (or XOAUTH2), STAT, UIDL, RETR, DELE; the messages kept on the card (their UIDL remembered). |
| SMTP | — | `smtp.h`: EHLO, STARTTLS or SSL, AUTH PLAIN / LOGIN / **XOAUTH2**, MAIL / RCPT / DATA; the message also put in Sent (IMAP: APPEND). |
| MIME | — | `mime.h`: reading (multipart, quoted-printable, base64, RFC 2047 headers, charsets — UTF-8, Latin-1, Windows-1252), writing (multipart/alternative text + HTML, attachments base64, UTF-8 headers). |
| HTML mail | NetSurf (Jet Browser) | a message's HTML shown **safely**: no remote pictures unless asked ("Show the pictures"), no scripts; first a simple HTML → text with its links and pictures (our own), Jet's engine later. |
| OAuth 2 (Outlook) | Courier's HTTPS (`http.hpp`) | the device code flow (`login.microsoftonline.com/consumers/oauth2/v2.0/devicecode`, then `/token` polled), the scopes `https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access`; the refresh token kept encrypted. Needs the **client id** of an application registered at Microsoft (below). |
| The cache | — | `SD:/mail/<account>/`: the folders' lists, the messages' headers (an index), the bodies fetched once, the attachments on demand; a worker thread syncs while the window stays live (kapi threads, as Media Player). |
| Secrets | — | the passwords and tokens **encrypted** on the card (a key of the card; the future key vault, HANDOFF's priority 5, takes them over). |
| Notifications | notifyd | "3 new messages" (who, the subject); the dock's badge. |
| Contacts | Cardfile's `model.h` (Writer's mail merge reads it) | the form made at first use, read / written; completion. |
| File associations | `fileassoc.ini` | `.eml` = mail (a message file opened); `mailto:` from Jet. |

## Outlook: registering "Onyx Mail" at Microsoft (once, by the user)

Microsoft's sign-in needs an **application id** (public, not a secret: it tells Microsoft which app asks). Free, with
a Microsoft account:

1. **portal.azure.com** (or **entra.microsoft.com**) → *Microsoft Entra ID* → **App registrations** → **New
   registration**. (A personal account may be asked to create its free "directory" first.)
2. Name **Onyx Mail**; *Supported account types*: **Accounts in any organizational directory and personal Microsoft
   accounts**; *Redirect URI*: none. **Register**.
3. **Authentication** → *Advanced settings* → **Allow public client flows: Yes** → Save (the device code needs it).
4. **API permissions** → *Add a permission* → *Microsoft Graph* → *Delegated*: `offline_access`,
   `IMAP.AccessAsUser.All`, `SMTP.Send` (and `email`, `openid`) → Add.
5. Copy the **Application (client) ID** (a GUID) from *Overview*: it goes into Mail (`SD:/etc/mail/oauth.ini`, or
   built in).

## To decide with the user

1. The name: **Mail** (folder `mail`)?
2. The layout: **three columns** (accounts and folders, the conversations, the message) as in the mock-ups — and
   the message alone in the window when it is narrow?
3. **Conversations** grouped (as Gmail) by default, or the messages one by one (a View option either way)?
4. **HTML messages**: shown as text with their links (safe and light) in the first version, Jet's engine later?
5. Contacts: `SD:/Documents/Contacts.card`, the fields of the mock-up?
