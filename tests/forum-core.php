<?php
declare(strict_types=1);
require dirname(__DIR__).'/Форум/app/core.php';
function check(bool $ok, string $name): void { if (!$ok) { fwrite(STDERR, "FAIL: $name\n"); exit(1); } echo "PASS: $name\n"; }
$db = init_db('Admin', 'StrongPass!123', 'admin@example.org');
check(count($db['boards']) === 12, 'initial community sections');
check(password_verify('StrongPass!123', $db['users'][0]['pass']), 'administrator password hashing');
$author = ['id'=>10, 'role'=>'member'];
$stranger = ['id'=>11, 'role'=>'member'];
$staff = ['id'=>1, 'role'=>'admin'];
$private = ['board_id'=>5, 'author_id'=>10, 'deleted'=>false];
$public = ['board_id'=>4, 'author_id'=>10, 'deleted'=>false];
check(thread_visible($db, $private, $author), 'author reads own private complaint');
check(!thread_visible($db, $private, $stranger), 'other members cannot read private complaint');
check(!thread_visible($db, $private, null), 'visitors cannot read private complaint');
check(thread_visible($db, $private, $staff), 'administrators can moderate private complaints');
check(thread_visible($db, $public, null), 'public board readable without login');
$private['deleted'] = true;
check(!thread_visible($db, $private, $author) && thread_visible($db, $private, $staff), 'deleted private thread visible only to staff');
check(!can_move_thread_to_board($db, $private, findrow($db['boards'], 4)), 'private complaints cannot move to public board');
check(can_move_thread_to_board($db, $private, findrow($db['boards'], 6)), 'private complaints may move to another private board');
check(can_change_board_privacy($db, 5, false), 'empty private board can become public');
$db['threads'][] = ['id'=>999, 'board_id'=>5, 'author_id'=>10, 'deleted'=>false];
check(!can_change_board_privacy($db, 5, false), 'private board with complaints cannot become public');
check(h('<script>') === '&lt;script&gt;', 'HTML escaping');
check(is_url('https://example.org/') && !is_url('javascript:alert(1)'), 'URL validation');
echo "Forum core tests: OK\n";
