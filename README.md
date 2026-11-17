# VELTOR ROLEPLAY

Independent VELTOR RolePlay community project. The repository contains **two separate PHP applications**: a site and a forum. These are not the official RADMIR RP sources or a complete playable game server.

## Structure

- `Сайт/` — PHP website, registration, admin tools, news, server listings and request-only store.
- `Форум/` — PHP forum, topics, complaints, roles, moderation and notifications.
- `Оформление/` — VK/Telegram visual assets.
- `tests/` — PHP core regression tests.
- `deploy/` — server configuration samples.

## Installation

**Requirements:** PHP 8.1+ with JSON, sessions and password hashing; writable `storage/`; Apache 2.4 with AllowOverride enabled (or Nginx with explicit deny rules).

1. Deploy `Сайт/` to the site's document root and `Форум/` to a different document root / subdomain.
2. Visit `https://your-site/install.php` and `https://your-forum/install.php` to create **different** administrator accounts.
3. Configure public links in both admin panels, enable HTTPS and back up both `storage/` directories.
4. **Do not** place runtime databases, backups, old SQL dumps, passwords or downloaded executables into Git.

The site and forum currently use **separate accounts**. Neither application accepts payments. The store records requests only. Game-to-web synchronization, email verification and real-time game monitoring are not implemented.

### Security

**Apache:** the committed `.htaccess` files block direct HTTP access to `app/` and `storage/`. Configure `AllowOverride` so Apache actually reads these files. Check that `/storage/database.lock` (after installation) returns **403**.

**Nginx:** `.htaccess` is ignored. Apply `deploy/nginx-restrictions.conf` to BOTH virtual hosts before exposing the services. Without these rules, runtime lock files and temporary data may be publicly readable.

Do not expose PHP's development server to the internet. Before production use, add automated backups, spam prevention and a persistent server monitoring strategy.

## Local checks

```sh
find 'Сайт' 'Форум' -type f -name '*.php' -print0 | xargs -0 -n1 php -l
php tests/site-core.php
php tests/forum-core.php
```

These checks do not replace end-to-end tests on the actual Apache/Nginx hosting environment.

Community: [VK](https://vk.ru/veltor_rp) · [Telegram](https://t.me/volterrp).
