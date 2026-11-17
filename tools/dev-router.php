<?php
declare(strict_types=1);

/**
 * Local development router only. Nginx/Apache must enforce the same denies.
 * Run: php -S 127.0.0.1:8000 -t 'Сайт' tools/dev-router.php
 */
$uri = (string) ($_SERVER['REQUEST_URI'] ?? '/');
$rawPath = parse_url($uri, PHP_URL_PATH);
if (!is_string($rawPath)) {
    http_response_code(400);
    exit('Bad request');
}
$path = rawurldecode($rawPath);
if (str_contains($path, "\0") || str_contains($path, '\\') || str_contains($path, '%')) {
    http_response_code(400);
    exit('Invalid path');
}
$parts = explode('/', trim($path, '/'));
$first = strtolower($parts[0] ?? '');
if (in_array($first, ['app', 'storage'], true)) {
    http_response_code(403);
    exit('Forbidden');
}
foreach ($parts as $part) {
    if ($part === '..' || $part === '.' || str_starts_with($part, '.')) {
        http_response_code(403);
        exit('Forbidden');
    }
}
if (preg_match('/\\.(?:sql|sqlite3?|db|lock|log|bak|tmp|old|env|ini|htaccess)\\z/i', $path)) {
    http_response_code(403);
    exit('Forbidden');
}
if (preg_match('/\\.php(?:\\/|$)/i', $path)
    && !in_array(ltrim($path, '/'), ['index.php', 'install.php'], true)) {
    http_response_code(403);
    exit('Forbidden');
}
return false;
