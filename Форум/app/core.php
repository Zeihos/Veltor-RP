<?php
declare(strict_types=1);
const FORUM_VERSION = '1.0.0';
const DATA_FILE = __DIR__.'/../storage/database.php';
const LOCK_FILE = __DIR__.'/../storage/.database.lock';
const DATA_PREFIX = "<?php exit; ?>\n";
function boot(): void {
    if (PHP_VERSION_ID < 80100) { http_response_code(500); exit('Нужен PHP 8.1 или новее'); }
    if (session_status() === PHP_SESSION_NONE) {
        $https = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
        session_name('VELTOR_FORUM_SESSION');
        session_set_cookie_params(['lifetime'=>0,'path'=>'/','secure'=>$https,'httponly'=>true,'samesite'=>'Lax']);
        session_start();
    }
    header('X-Content-Type-Options: nosniff');
    header('X-Frame-Options: DENY');
    header('Referrer-Policy: strict-origin-when-cross-origin');
    header('Permissions-Policy: camera=(), microphone=(), geolocation=()');
    header("Content-Security-Policy: default-src 'self'; img-src 'self' data:; font-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; object-src 'none'; form-action 'self'; frame-ancestors 'none'");
}
function db_read(): ?array {
    if (!is_file(DATA_FILE)) return null;
    $l=fopen(LOCK_FILE,'c'); if(!$l || !flock($l,LOCK_SH)) throw new RuntimeException('Не удалось открыть базу');
    try { $r=file_get_contents(DATA_FILE); if($r===false || !str_starts_with($r,DATA_PREFIX)) throw new RuntimeException('Повреждён формат хранилища');
        $v=json_decode(substr($r,strlen(DATA_PREFIX)),true,512,JSON_THROW_ON_ERROR);
        if(!is_array($v)) throw new RuntimeException('Ошибка хранилища'); return $v;
    } finally { flock($l,LOCK_UN); fclose($l); }
}
function db_change(callable $fn): mixed {
    $d=dirname(DATA_FILE); if(!is_dir($d) && !mkdir($d,0700,true) && !is_dir($d)) throw new RuntimeException('Не удалось создать хранилище');
    $l=fopen(LOCK_FILE,'c'); if(!$l || !flock($l,LOCK_EX)) throw new RuntimeException('Хранилище недоступно');
    try {
        $db=null;
        if(is_file(DATA_FILE)) { $r=file_get_contents(DATA_FILE); if($r===false || !str_starts_with($r,DATA_PREFIX)) throw new RuntimeException('Повреждён формат базы'); $db=json_decode(substr($r,strlen(DATA_PREFIX)),true,512,JSON_THROW_ON_ERROR); }
        $result=$fn($db);
        if($db!==null) {
            $tmp=tempnam($d,'.forum-'); if($tmp===false) throw new RuntimeException('Не удалось сохранить данные');
            $body=DATA_PREFIX.json_encode($db,JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES|JSON_THROW_ON_ERROR);
            if(file_put_contents($tmp,$body)===false) { @unlink($tmp); throw new RuntimeException('Ошибка записи'); }
            @chmod($tmp,0600);
            if(!rename($tmp,DATA_FILE)) { @unlink($tmp); throw new RuntimeException('Ошибка сохранения'); }
        }
        return $result;
    } finally { flock($l,LOCK_UN); fclose($l); }
}
function init_db(string $name,string $pass,string $email): array {
    $now=time();
    $categories=[
      ['id'=>1,'title'=>'ИНФОРМАЦИОННЫЙ РАЗДЕЛ','description'=>'Всё, что нужно знать о проекте','sort'=>10],
      ['id'=>2,'title'=>'ИГРОВЫЕ СЕРВЕРЫ','description'=>'Обращения игроков, взаимодействие с командой','sort'=>20],
      ['id'=>3,'title'=>'СООБЩЕСТВО VELTOR','description'=>'Общение, творчество и развитие','sort'=>30]
    ];
    $boards=[
      ['id'=>1,'category_id'=>1,'title'=>'Новости и объявления','description'=>'Важная информация от руководства проекта','icon'=>'megaphone','only_staff'=>true,'private'=>false,'sort'=>1],
      ['id'=>2,'category_id'=>1,'title'=>'Правила проекта','description'=>'Регламент проекта и правила поведения','icon'=>'shield','only_staff'=>true,'private'=>false,'sort'=>2],
      ['id'=>3,'category_id'=>1,'title'=>'Вопросы и помощь','description'=>'Помощь новичкам и ответы на вопросы','icon'=>'help','only_staff'=>false,'private'=>false,'sort'=>3],
      ['id'=>4,'category_id'=>2,'title'=>'Общение игроков','description'=>'Обсуждение игровых событий и серверов','icon'=>'game','only_staff'=>false,'private'=>false,'sort'=>1],
      ['id'=>5,'category_id'=>2,'title'=>'Жалобы на игроков','description'=>'Приватные обращения к администрации','icon'=>'flag','only_staff'=>false,'private'=>true,'sort'=>2],
      ['id'=>6,'category_id'=>2,'title'=>'Жалобы на администрацию','description'=>'Приватные обращения к руководству','icon'=>'shield','only_staff'=>false,'private'=>true,'sort'=>3],
      ['id'=>7,'category_id'=>2,'title'=>'Обжалование наказаний','description'=>'Приватные апелляции и пересмотр решений','icon'=>'scale','only_staff'=>false,'private'=>true,'sort'=>4],
      ['id'=>8,'category_id'=>2,'title'=>'Заявки в команду','description'=>'Приватные заявки на наборы и должности','icon'=>'users','only_staff'=>false,'private'=>true,'sort'=>5],
      ['id'=>9,'category_id'=>2,'title'=>'Техническая поддержка','description'=>'Сообщения об ошибках и неисправностях','icon'=>'tool','only_staff'=>false,'private'=>true,'sort'=>6],
      ['id'=>10,'category_id'=>3,'title'=>'Предложения по улучшению','description'=>'Ваши идеи и предложения для VELTOR','icon'=>'spark','only_staff'=>false,'private'=>false,'sort'=>1],
      ['id'=>11,'category_id'=>3,'title'=>'Творчество и медиа','description'=>'Скриншоты, видео, фан-работы','icon'=>'image','only_staff'=>false,'private'=>false,'sort'=>2],
      ['id'=>12,'category_id'=>3,'title'=>'Свободное общение','description'=>'Беседы участников сообщества','icon'=>'chat','only_staff'=>false,'private'=>false,'sort'=>3]
    ];
    $db=['version'=>1,'next_id'=>13,'users'=>[['id'=>1,'username'=>$name,'email'=>strtolower($email),'pass'=>password_hash($pass,PASSWORD_DEFAULT),'role'=>'admin','banned'=>false,'created'=>$now,'last_seen'=>$now,'bio'=>'Руководство VELTOR ROLEPLAY']],
       'categories'=>$categories,'boards'=>$boards,'threads'=>[],'posts'=>[],'reactions'=>[],'subscriptions'=>[],'bookmarks'=>[],'notifications'=>[],'reports'=>[],'audit'=>[],'attempts'=>[],
       'settings'=>['title'=>'VELTOR ROLEPLAY','subtitle'=>'Официальный форум игрового сообщества','announcement'=>'Добро пожаловать на официальный форум VELTOR ROLEPLAY! Следите за новостями и обсуждениями.','vk'=>'https://vk.ru/veltor_rp','tg'=>'https://t.me/volterrp','site_url'=>'','registration'=>'1','rules'=>'Уважайте собеседников. Запрещены спам, оскорбления, мошенничество, публикация персональных данных и обход блокировок. При создании жалобы приложите необходимые доказательства.','footer'=>'VELTOR ROLEPLAY — независимый игровой проект.'] ];
    $samples=[
      [1,'Добро пожаловать на форум VELTOR ROLEPLAY','Это официальный форум проекта VELTOR ROLEPLAY. Здесь будут публиковаться новости, объявления, правила, ответы администрации и материалы сообщества.\n\nСледите за нашими социальными сетями: VK — https://vk.ru/veltor_rp, Telegram — https://t.me/volterrp.\n\nСведения о запуске игровых серверов будут добавлены администрацией после подтверждения.'],
      [2,'Правила форума','1. Соблюдайте уважительный тон общения.\n2. Не публикуйте спам, рекламу, вредоносные ссылки и персональные данные.\n3. Не выдавайте себя за администрацию.\n4. Оформляйте обращения в соответствующих разделах.\n5. Не публикуйте заведомо ложные жалобы и документы.\n6. Решения модерации можно оспорить в специальном разделе.\n\nРедакция правил может дополняться администрацией.'],
      [3,'Как правильно задать вопрос','Напишите краткий заголовок, подробно опишите ситуацию и укажите, что вы уже пробовали сделать. Для обращения к администрации по конкретному нарушению используйте закрытые разделы жалоб.']
    ];
    foreach($samples as $i=>$x) {
      $x[2]=str_replace('\\n', "\n", $x[2]);
      $tid=$db['next_id']++;$pid=$db['next_id']++;
      $db['threads'][]=['id'=>$tid,'board_id'=>$x[0],'title'=>$x[1],'author_id'=>1,'created'=>$now-240+$i,'updated'=>$now-240+$i,'last_author_id'=>1,'pinned'=>true,'locked'=>$x[0]!==3,'deleted'=>false,'views'=>0];
      $db['posts'][]=['id'=>$pid,'thread_id'=>$tid,'author_id'=>1,'body'=>$x[2],'created'=>$now-240+$i,'updated'=>null,'deleted'=>false];
    }
    return $db;
}
function newid(array &$db): int {return $db['next_id']++;}
function h(mixed $s): string {return htmlspecialchars((string)$s,ENT_QUOTES|ENT_SUBSTITUTE,'UTF-8');}
function arg(string $name,int $default=0): int {return max(0,(int)($_GET[$name]??$default));}
function inp(string $name,int $max=10000): string {$val=$_POST[$name]??''; return is_string($val)?trim(substr($val,0,$max*4)):'';}
function cut(string $str,int $max): string {return function_exists('mb_substr')?mb_substr($str,0,$max):substr($str,0,$max*4);}
function length_s(string $str): int {return function_exists('mb_strlen')?mb_strlen($str,'UTF-8'):strlen($str);}
function link_to(string $p='home',array $extra=[]): string {if($p!=='home') $extra=['p'=>$p]+$extra; return 'index.php'.($extra?'?'.http_build_query($extra,'','&',PHP_QUERY_RFC3986):'');}
function back_to(string $p='home',array $extra=[]): never {header('Location: '.link_to($p,$extra),true,303);exit;}
function field_csrf(): string {return '<input type="hidden" name="csrf" value="'.h(csrf()).'">';}
function csrf(): string {if(!isset($_SESSION['csrf'])) $_SESSION['csrf']=bin2hex(random_bytes(32));return $_SESSION['csrf'];}
function verify_csrf(): void {if(!hash_equals(csrf(),(string)($_POST['csrf']??''))) {http_response_code(403);exit('Недействительный токен формы');}}
function alert(string $message,string $kind='success'): void {$_SESSION['flash']=['message'=>$message,'kind'=>$kind];}
function render_alert(): void {if(!empty($_SESSION['flash'])){$f=$_SESSION['flash'];unset($_SESSION['flash']);echo '<div class="alert '.h($f['kind']).'" role="alert">'.h($f['message']).'</div>';}}
function demand(bool $cond,string $msg): void {if(!$cond) throw new DomainException($msg);}
function findrow(array $rows,int $id): ?array {foreach($rows as $r) if((int)$r['id']===$id) return $r;return null;}
function current_user(array $db): ?array {$id=(int)($_SESSION['uid']??0);$u=findrow($db['users'],$id);return $u && empty($u['banned'])?$u:null;}
function user_name(array $db,int $id): string {return (string)(findrow($db['users'],$id)['username']??'Участник');}
function staff(?array $u): bool {return $u!==null && in_array($u['role'],['admin','moderator'],true);}
function admin(?array $u): bool {return $u!==null && $u['role']==='admin';}
function need_login(?array $u): void {if(!$u){alert('Сначала войдите в аккаунт.','error');back_to('login');}}
function need_staff(?array $u): void {demand(staff($u),'Недостаточно прав');}
function need_admin(?array $u): void {demand(admin($u),'Недостаточно прав');}
function board_visible(array $b,?array $u): bool {return empty($b['private'])||staff($u);}
function thread_visible(array $db,array $t,?array $u): bool {
    $b=findrow($db['boards'],(int)$t['board_id']);
    return (empty($t['deleted'])||staff($u)) && $b!==null && (empty($b['private']) || staff($u) || ($u!==null && (int)$t['author_id']===(int)$u['id']));
}
function can_move_thread_to_board(array $db,array $thread,array $destination): bool {
    $source=findrow($db['boards'],(int)$thread['board_id']);
    return $source!==null && !( !empty($source['private']) && empty($destination['private']));
}
function can_change_board_privacy(array $db,int $boardId,bool $bePrivate): bool {
    if($bePrivate)return true;
    $board=findrow($db['boards'],$boardId);
    if($board===null)return false;
    if(empty($board['private']))return true;
    foreach($db['threads'] as $thread)if((int)$thread['board_id']===$boardId)return false;
    return true;
}
function can_start(array $b,?array $u): bool {return $u!==null && (empty($b['only_staff'])||staff($u));}
function topic_posts(array $db,int $tid): array {return array_values(array_filter($db['posts'],fn($p)=>(int)$p['thread_id']===$tid && empty($p['deleted'])));}
function human_date(int $ts): string {return date('d.m.Y, H:i',$ts);}
function is_url(string $url): bool {return $url==='' || (strlen($url)<=500 && filter_var($url,FILTER_VALIDATE_URL) && in_array(strtolower((string)parse_url($url,PHP_URL_SCHEME)),['http','https'],true));}
function limiter(array &$db,string $key,int $seconds): void {
    $now=time();foreach($db['attempts'] as $k=>$a) if(($a['t']??0)<$now-86400) unset($db['attempts'][$k]);
    demand((int)($db['attempts'][$key]['t']??0)+$seconds <= $now,'Действие выполняется слишком часто. Повторите позже.');
    $db['attempts'][$key]=['t'=>$now,'n'=>1];
}
function login_limit(array &$db,string $key): void {
    $now=time();$r=$db['attempts'][$key]??['n'=>0,'t'=>0]; if($r['t']<$now-900)$r=['n'=>0,'t'=>$now];
    demand($r['n']<7,'Слишком много попыток входа. Повторите через 15 минут.');
    $db['attempts'][$key]=['n'=>$r['n']+1,'t'=>$now];
}
function session_login(int $id): void {session_regenerate_id(true);$_SESSION['uid']=$id;unset($_SESSION['csrf']);}
function session_logout(): void {$_SESSION=[];session_regenerate_id(true);}
function notify(array &$db,int $target,string $message,string $dest,int $actor=0): void {
    if($target===$actor||$target<1)return;
    $db['notifications'][]=['id'=>newid($db),'user_id'=>$target,'message'=>cut($message,180),'url'=>$dest,'created'=>time(),'read'=>false];
    if(count($db['notifications'])>4000)$db['notifications']=array_slice($db['notifications'],-3500);
}
function audit(array &$db,int $actor,string $action,string $context): void {
    $db['audit'][]=['at'=>time(),'actor'=>$actor,'action'=>$action,'context'=>cut($context,180)];
    if(count($db['audit'])>500)$db['audit']=array_slice($db['audit'],-400);
}
function escape_body(string $text): string {return nl2br(h($text),false);}
function role_label(string $role): string {return ['admin'=>'Руководство','moderator'=>'Модератор','member'=>'Участник'][$role]??'Участник';}
function icon_svg(string $name): string {
   $paths=['megaphone'=>'<path d="M3 11v2h4l9 5V6l-9 5H3zm4 2 1.5 6H11l-2-6M19 9q3 3 0 6"/>','shield'=>'<path d="M12 2 3 6v6c0 6 4.5 9 9 10 4.5-1 9-4 9-10V6z"/><path d="m9 12 2 2 4-4"/>','help'=>'<circle cx="12" cy="12" r="10"/><path d="M9 9a3 3 0 1 1 5 2c-1.5 1-2 1.5-2 3M12 18h.01"/>','game'=>'<path d="M6 7h12c2 0 3 2 4 7s-2 7-5 4l-2-2H9l-2 2c-3 3-6 1-5-4s2-7 4-7zM6 12h6M9 9v6M17 12h.01M19 10h.01"/>','flag'=>'<path d="M5 22V3m0 1q5-3 9 0t8 0v12q-4 3-8 0t-9 0"/>','scale'=>'<path d="M12 3v18M4 7h16M4 7l-3 7h6zM20 7l-3 7h6zM7 21h10"/>','users'=>'<circle cx="9" cy="8" r="3"/><path d="M3 20v-2c0-4 12-4 12 0v2M17 5a3 3 0 0 1 0 6M17 14c3 0 5 2 5 5v1"/>','tool'=>'<path d="M20 7a6 6 0 0 1-7 7l-7 7-3-3 7-7a6 6 0 0 1 7-7l-4 4 3 3z"/>','spark'=>'<path d="m12 2 2.5 7.5L22 12l-7.5 2.5L12 22l-2.5-7.5L2 12l7.5-2.5z"/>','image'=>'<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8.5" cy="8.5" r="2"/><path d="m3 17 5-5 4 4 3-3 6 5"/>','chat'=>'<path d="M21 11a8 8 0 0 1-8 8H6l-4 3V12a9 9 0 0 1 9-9 10 10 0 0 1 10 8z"/>'];
   return '<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">'.($paths[$name]??$paths['chat']).'</svg>';
}
