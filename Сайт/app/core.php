<?php
declare(strict_types=1);
const VELTOR_VERSION = '1.0.0';
const DATA_FILE = __DIR__ . '/../storage/database.php';
const LOCK_FILE = __DIR__ . '/../storage/database.lock';
const DB_PREFIX = "<?php exit; ?>\n";

function boot(): void {
    if (PHP_VERSION_ID < 80100) { http_response_code(500); exit('Требуется PHP 8.1 или новее.'); }
    if (session_status() === PHP_SESSION_NONE) {
        $secure = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
        session_name('VELTOR_SESSID');
        session_set_cookie_params(['lifetime'=>0,'path'=>'/','secure'=>$secure,'httponly'=>true,'samesite'=>'Lax']);
        session_start();
    }
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: strict-origin-when-cross-origin');
    header('X-Frame-Options: DENY');
    header('Permissions-Policy: camera=(), microphone=(), geolocation=()');
    header('Content-Security-Policy: default-src \'self\'; img-src \'self\' data:; style-src \'self\'; script-src \'self\'; font-src \'self\'; base-uri \'self\'; form-action \'self\'; frame-ancestors \'none\'');
}
function initial_data(string $name, string $password): array {
    return [
        'version'=>1, 'next_id'=>2, 'users'=>[['id'=>1,'username'=>$name,'email'=>'','password'=>password_hash($password,PASSWORD_DEFAULT),'role'=>'admin','created'=>time(),'banned'=>false,'nickname'=>'']],
        'news'=>[], 'servers'=>[], 'products'=>[], 'orders'=>[], 'topics'=>[], 'posts'=>[], 'tickets'=>[], 'ticket_replies'=>[], 'messages'=>[], 'attempts'=>[],
        'settings'=>['site_name'=>'VELTOR ROLEPLAY','tagline'=>'ТВОЯ ИСТОРИЯ НАЧИНАЕТСЯ ЗДЕСЬ','description'=>'Официальный портал VELTOR ROLEPLAY. Новости, серверы и сообщество.','vk_url'=>'https://vk.ru/veltor_rp','tg_url'=>'https://t.me/volterrp','launcher_url'=>'','support_email'=>'','announcement'=>'Следите за анонсами запуска проекта в наших сообществах.','start_date'=>'','registration_open'=>'1','shop_open'=>'1']
    ];
}
function store_read(): ?array {
    if (!is_file(DATA_FILE)) return null;
    $handle = fopen(LOCK_FILE,'c');
    if (!$handle || !flock($handle,LOCK_SH)) throw new RuntimeException('Не удалось открыть хранилище.');
    try {
        $raw = file_get_contents(DATA_FILE);
        if ($raw === false || !str_starts_with($raw,DB_PREFIX)) throw new RuntimeException('Повреждён формат хранилища.');
        $data = json_decode(substr($raw,strlen(DB_PREFIX)),true,512,JSON_THROW_ON_ERROR);
        if (!is_array($data)) throw new RuntimeException('Повреждены данные.');
        return $data;
    } finally { flock($handle,LOCK_UN); fclose($handle); }
}
function store_write(callable $fn): mixed {
    $dir = dirname(DATA_FILE);
    if (!is_dir($dir) && !mkdir($dir,0700,true) && !is_dir($dir)) throw new RuntimeException('Невозможно создать каталог storage.');
    $lock = fopen(LOCK_FILE,'c');
    if (!$lock || !flock($lock,LOCK_EX)) throw new RuntimeException('Нет доступа к storage.');
    try {
        $data = null;
        if (is_file(DATA_FILE)) {
            $raw = file_get_contents(DATA_FILE);
            if ($raw === false || !str_starts_with($raw,DB_PREFIX)) throw new RuntimeException('Ошибка чтения базы данных.');
            $data = json_decode(substr($raw,strlen(DB_PREFIX)),true,512,JSON_THROW_ON_ERROR);
        }
        $res = $fn($data);
        if ($data !== null) {
            $encoded = DB_PREFIX . json_encode($data,JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES|JSON_THROW_ON_ERROR|JSON_PRETTY_PRINT);
            $tmp = tempnam($dir,'.veltor-');
            if ($tmp === false) throw new RuntimeException('Невозможно сохранить базу.');
            if (file_put_contents($tmp,$encoded) === false) { @unlink($tmp); throw new RuntimeException('Не удалось записать файл.'); }
            @chmod($tmp,0600);
            if (!rename($tmp,DATA_FILE)) { @unlink($tmp); throw new RuntimeException('Не удалось применить изменения.'); }
        }
        return $res;
    } finally { flock($lock,LOCK_UN); fclose($lock); }
}
function new_id(array &$db): int { return $db['next_id']++; }
function e(mixed $value): string { return htmlspecialchars((string)$value,ENT_QUOTES|ENT_SUBSTITUTE,'UTF-8'); }
function url(string $page='',array $args=[]): string {
    if ($page !== '' && $page !== 'home') $args = ['p'=>$page] + $args;
    return 'index.php'.($args ? '?'.http_build_query($args,'','&',PHP_QUERY_RFC3986) : '');
}
function go(string $page='home',array $args=[]): never { header('Location: '.url($page,$args),true,303); exit; }
function val(string $key,int $limit=2000): string { return trim(substr((string)($_POST[$key] ?? ''),0,$limit*4)); }
function token(): string { if (empty($_SESSION['csrf'])) $_SESSION['csrf']=bin2hex(random_bytes(32)); return (string)$_SESSION['csrf']; }
function csrf_field(): string { return '<input type="hidden" name="csrf" value="'.e(token()).'">'; }
function verify_csrf(): void { if (!hash_equals(token(),(string)($_POST['csrf']??''))) { http_response_code(403); exit('Неверный токен формы. Обновите страницу.'); } }
function flash(string $text,string $type='ok'): void { $_SESSION['flash']=['text'=>$text,'type'=>$type]; }
function flashes(): void {
    if (isset($_SESSION['flash'])) { $f=$_SESSION['flash']; unset($_SESSION['flash']); echo '<div class="flash '.e($f['type']).'" role="status">'.e($f['text']).'</div>'; }
}
function logged_user(array $db): ?array {
    $id=(int)($_SESSION['user_id']??0);
    foreach ($db['users'] as $u) if ($u['id']===$id && empty($u['banned'])) return $u;
    if ($id) unset($_SESSION['user_id']);
    return null;
}
function must_login(?array $user): void { if (!$user) {flash('Войдите в аккаунт для продолжения.','error');go('login');} }
function must_admin(?array $user): void { if (!$user || $user['role']!=='admin') {http_response_code(403);exit('Доступ запрещён.');} }
function one(array $data,string $table,int $id): ?array { foreach ($data[$table] ?? [] as $item) if ((int)$item['id']===$id) return $item; return null; }
function sorted(array $rows,string $key='created'): array { usort($rows,fn($a,$b)=>($b[$key]??0)<=>($a[$key]??0));return $rows; }
function setting(array $db,string $key): string { return (string)($db['settings'][$key]??''); }
function date_fmt(int $time): string { return date('d.m.Y H:i',$time); }
function is_valid_http_url(string $u): bool { return $u==='' || (strlen($u)<500 && filter_var($u,FILTER_VALIDATE_URL) && in_array(strtolower((string)parse_url($u,PHP_URL_SCHEME)),['http','https'],true)); }
function ensure(bool $ok,string $message): void { if (!$ok) throw new DomainException($message); }
function check_password(string $pass): void { ensure(strlen($pass)>=10 && strlen($pass)<=200,'Пароль должен содержать от 10 до 200 символов.'); }
function user_by_name(array $data,string $name): ?array {foreach($data['users'] as $u) if(strcasecmp($u['username'],$name)===0)return $u;return null;}
function limited(string $s,int $max=150): string { return function_exists('mb_substr')?mb_substr($s,0,$max):substr($s,0,$max*4); }
function throttle(array &$db,string $key): void {
    $now=time(); foreach($db['attempts'] as $k=>$a) if(($a['time']??0)<$now-3600) unset($db['attempts'][$k]);
    $record=$db['attempts'][$key]??['count'=>0,'time'=>$now];
    ensure($record['count']<8 || $record['time']<$now-900,'Слишком много попыток. Повторите через 15 минут.');
    $db['attempts'][$key]=['count'=>($record['time']<$now-900?1:$record['count']+1),'time'=>$now];
}
function rate_limit_write(array &$db,string $key,int $seconds): void {
    $now=time();$prev=(int)($db['attempts'][$key]['time']??0);ensure($prev+$seconds<=$now,'Слишком частые действия. Попробуйте немного позже.');$db['attempts'][$key]=['count'=>1,'time'=>$now];
}
function start_session(int $id): void {session_regenerate_id(true);$_SESSION['user_id']=$id;}
function is_post(): bool {return ($_SERVER['REQUEST_METHOD']??'GET')==='POST';}
function render_header(string $title,string $active,array $db,?array $user): void {
    $site=setting($db,'site_name'); $desc=setting($db,'description');
    $nav=['home'=>'Главная','play'=>'Как начать','servers'=>'Серверы','news'=>'Новости','store'=>'Магазин','forum'=>'Форум','contacts'=>'Контакты'];
    ?><!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#090b10"><meta name="description" content="<?=e($desc)?>"><meta property="og:type" content="website"><meta property="og:title" content="<?=e($title.' | '.$site)?>"><meta property="og:description" content="<?=e($desc)?>"><meta property="og:image" content="assets/images/veltor-cover.jpg"><link rel="icon" href="assets/images/favicon.png"><link rel="stylesheet" href="assets/css/style.css?v=2"><link rel="stylesheet" href="assets/css/app.css?v=2"><script defer src="assets/js/main.js?v=2"></script><title><?=e($title.' | '.$site)?></title></head><body><a class="skip" href="#main">Перейти к содержимому</a><header class="site-header"><div class="container header-inner"><a class="brand" href="<?=url()?>"><div class="brand-mark"><span>V</span></div><div class="brand-name"><strong>VELTOR</strong><small>ROLEPLAY</small></div></a><nav id="site-nav" class="nav" aria-label="Навигация"><?php foreach($nav as $path=>$label):?><a class="<?=($active===$path?'active':'')?>" href="<?=e(url($path))?>"><?=e($label)?></a><?php endforeach;?><a class="nav-user" href="<?=url($user?'account':'login')?>"><?=e($user?$user['username']:'Войти')?></a><?php if($user && $user['role']==='admin'):?><a href="<?=url('admin')?>" class="nav-admin">Панель</a><?php endif;?></nav><a href="<?=url($user?'account':'register')?>" class="btn btn-primary header-cta"><?=e($user?'Кабинет':'Регистрация')?> ↗</a><button class="menu-button" type="button" aria-controls="site-nav" aria-expanded="false" aria-label="Открыть меню" data-menu-toggle><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg></button></div></header><main id="main"><div class="container" style="padding-top:15px"><?php flashes();?></div><?php
}
function render_footer(array $db): void { ?></main><footer class="site-footer"><div class="container"><div class="footer-top"><div><a class="brand" href="<?=url()?>"><div class="brand-mark"><span>V</span></div><div class="brand-name"><strong>VELTOR</strong><small>ROLEPLAY</small></div></a><p><?=e(setting($db,'description'))?></p></div><div class="footer-links"><strong>Проект</strong><a href="<?=url('play')?>">Как начать</a><a href="<?=url('servers')?>">Серверы</a><a href="<?=url('news')?>">Новости</a><a href="<?=url('forum')?>">Форум</a></div><div class="footer-links"><strong>Информация</strong><a href="<?=url('store')?>">Магазин</a><a href="<?=url('contacts')?>">Контакты</a><a href="<?=url('privacy')?>">Конфиденциальность</a><a href="<?=e(setting($db,'vk_url'))?>" target="_blank" rel="noopener noreferrer">ВКонтакте ↗</a><a href="<?=e(setting($db,'tg_url'))?>" target="_blank" rel="noopener noreferrer">Telegram ↗</a></div></div><div class="footer-bottom"><span>© <?=date('Y')?> VELTOR ROLEPLAY. Все права защищены.</span><span>Версия сайта <?=VELTOR_VERSION?></span></div></div></footer></body></html><?php }
function page_head(string $eyebrow,string $title,string $desc=''): void { echo '<section class="page-head"><div class="container"><span class="eyebrow">'.e($eyebrow).'</span><h1 class="display">'.e($title).'</h1>'.($desc?'<p class="lead">'.e($desc).'</p>':'').'</div></section>'; }
function empty_state(string $title,string $desc): void {echo '<div class="panel empty"><div class="card-icon">✦</div><h2>'.e($title).'</h2><p>'.e($desc).'</p></div>';}
function textarea_content(string $s): string {return nl2br(e($s));}
function badge(string $s): string {return '<span class="pill">'.e($s).'</span>';}
function field(string $name,string $label,string $value='',string $type='text',bool $required=false,string $attrs=''): void { echo '<label class="field"><span>'.e($label).'</span><input name="'.e($name).'" type="'.e($type).'" value="'.e($value).'" '.($required?'required ':'').$attrs.'></label>'; }
function area(string $name,string $label,string $value='',bool $required=false): void {echo '<label class="field"><span>'.e($label).'</span><textarea name="'.e($name).'" rows="6" '.($required?'required':'').'>'.e($value).'</textarea></label>';}
function action_field(string $action): void {echo csrf_field().'<input type="hidden" name="action" value="'.e($action).'">';}
function button(string $label='Сохранить'): void {echo '<button class="btn btn-primary" type="submit">'.e($label).'</button>';}
function admin_links(string $active): void { $items=['admin'=>'Обзор','admin-news'=>'Новости','admin-servers'=>'Серверы','admin-store'=>'Товары','admin-orders'=>'Заказы','admin-users'=>'Пользователи','admin-forum'=>'Форум','admin-tickets'=>'Обращения','admin-messages'=>'Сообщения','admin-settings'=>'Настройки'];echo '<nav class="admin-nav" aria-label="Разделы администрирования">';foreach($items as $key=>$title) echo '<a class="'.($active===$key?'selected':'').'" href="'.url($key).'">'.e($title).'</a>';echo '</nav>';}
function status_text(string $status): string {return ['new'=>'Новый','processing'=>'В работе','completed'=>'Выполнено','cancelled'=>'Отменено','open'=>'Открыто','closed'=>'Закрыто','published'=>'Опубликовано','draft'=>'Черновик'][$status]??$status;}
