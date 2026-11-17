<?php
declare(strict_types=1);
require dirname(__DIR__).'/Сайт/app/core.php';
function check(bool $ok, string $name): void { if (!$ok) { fwrite(STDERR, "FAIL: $name\n"); exit(1); } echo "PASS: $name\n"; }
$seed = initial_data('Admin', 'StrongPass!123');
check(count($seed['users']) === 1 && $seed['users'][0]['role'] === 'admin', 'initial administrator');
check(password_verify('StrongPass!123', $seed['users'][0]['password']), 'admin password hashing');
check($seed['settings']['vk_url'] === 'https://vk.ru/veltor_rp', 'VK brand link');
check($seed['settings']['tg_url'] === 'https://t.me/volterrp', 'Telegram brand link');
check(e('<script>') === '&lt;script&gt;', 'HTML output escaping');
check(is_valid_http_url('https://example.org/path') && !is_valid_http_url('javascript:alert(1)'), 'external URL validation');
check(user_by_name($seed, 'admin') !== null, 'case-insensitive account lookup');
check(str_contains(url('news', ['id'=>7]), 'p=news'), 'internal page routing');
echo "Site core tests: OK\n";
