<?php
declare(strict_types=1);
if(($_SERVER['REQUEST_METHOD']??'GET')!=='POST') return;
verify_csrf();
$action=inp('action',50);
$id=max(0,(int)($_POST['id']??0));
$page=inp('_page',40);$page=in_array($page,['home','board','thread','new','login','register','profile','account','admin','reports'],true)?$page:'home';
$pageId=max(0,(int)($_POST['_id']??0));
try {
    if($action==='register') {
        $name=inp('username',32);$email=strtolower(inp('email',180));$pass=(string)($_POST['password']??'');
        demand(inp('website')==='','Ошибка отправки формы');
        demand((bool)preg_match('/^[\p{L}\p{N}_-]{3,24}$/u',$name),'Имя: 3–24 буквы, цифры, дефисы или подчёркивания');
        demand((bool)filter_var($email,FILTER_VALIDATE_EMAIL) && strlen($email)<=180,'Укажите действительный адрес электронной почты');
        demand(strlen($pass)>=10 && strlen($pass)<=200,'Пароль должен содержать от 10 до 200 символов');
        $uid=db_change(function (&$db) use($name,$email,$pass){
            demand(($db['settings']['registration']??'1')==='1','Регистрация временно закрыта');
            limiter($db,'reg:'.hash('sha256',(string)($_SERVER['REMOTE_ADDR']??'')),90);
            foreach($db['users'] as $u) demand(strcasecmp($u['username'],$name)!==0 && strtolower($u['email'])!==$email,'Имя или электронная почта уже используются');
            $uid=newid($db);$db['users'][]=['id'=>$uid,'username'=>$name,'email'=>$email,'pass'=>password_hash($pass,PASSWORD_DEFAULT),'role'=>'member','banned'=>false,'created'=>time(),'last_seen'=>time(),'bio'=>''];return $uid;
        });
        session_login($uid);alert('Добро пожаловать на форум VELTOR!');back_to('account');
    }
    if($action==='login') {
        $name=inp('username',180);$pass=(string)($_POST['password']??'');
        $uid=db_change(function (&$db) use($name,$pass){
            $key='login:'.hash('sha256',(string)($_SERVER['REMOTE_ADDR']??''));login_limit($db,$key);
            foreach($db['users'] as $u) if(strcasecmp($u['username'],$name)===0 || strcasecmp($u['email'],$name)===0) {
                if(!empty($u['banned']))return -1;
                if(password_verify($pass,$u['pass'])) {unset($db['attempts'][$key]);return (int)$u['id'];}
            }
            return 0;
        });
        demand($uid!==-1,'Доступ к форуму ограничен');
        demand($uid>0,'Неправильный логин или пароль');
        session_login($uid);alert('Вы вошли в аккаунт');back_to('home');
    }
    $db=db_read();demand($db!==null,'Форум не установлен');$u=current_user($db);
    if($action==='logout') {session_logout();header('Location: index.php',true,303);exit;}
    need_login($u);
    if($action==='new-thread') {
        $boardId=max(0,(int)($_POST['board_id']??0));$title=inp('title',150);$body=inp('body',15000);
        demand(length_s($title)>=5 && length_s($title)<=120,'Заголовок должен содержать 5–120 символов');
        demand(length_s($body)>=15 && length_s($body)<=12000,'Текст темы должен содержать 15–12000 символов');
        $tid=db_change(function (&$db) use($u,$boardId,$title,$body){
            $b=findrow($db['boards'],$boardId);demand($b!==null,'Раздел не найден');demand(can_start($b,$u),'Нельзя создавать темы в этом разделе');
            limiter($db,'thread:'.$u['id'],25);$tid=newid($db);$now=time();
            $db['threads'][]=['id'=>$tid,'board_id'=>$boardId,'title'=>$title,'author_id'=>$u['id'],'created'=>$now,'updated'=>$now,'last_author_id'=>$u['id'],'pinned'=>false,'locked'=>false,'deleted'=>false,'views'=>0];
            $db['posts'][]=['id'=>newid($db),'thread_id'=>$tid,'author_id'=>$u['id'],'body'=>$body,'created'=>$now,'updated'=>null,'deleted'=>false];
            return $tid;
        });alert('Тема опубликована');back_to('thread',['id'=>$tid]);
    }
    if($action==='reply') {
        $tid=max(0,(int)($_POST['thread_id']??0));$body=inp('body',15000);demand(length_s($body)>=2 && length_s($body)<=12000,'Сообщение должно содержать 2–12000 символов');
        db_change(function (&$db) use($u,$tid,$body){
            $t=findrow($db['threads'],$tid);demand($t!==null && thread_visible($db,$t,$u),'Тема недоступна');demand(!$t['locked']||staff($u),'Обсуждение закрыто');
            limiter($db,'reply:'.$u['id'],6);$now=time();
            $db['posts'][]=['id'=>newid($db),'thread_id'=>$tid,'author_id'=>$u['id'],'body'=>$body,'created'=>$now,'updated'=>null,'deleted'=>false];
            foreach($db['threads'] as &$tr) if((int)$tr['id']===$tid){$tr['updated']=$now;$tr['last_author_id']=$u['id'];break;}unset($tr);
            $to=[$t['author_id']];foreach($db['subscriptions'] as $sub) if((int)$sub['thread_id']===$tid)$to[]=(int)$sub['user_id'];
            foreach(array_unique($to) as $recipient) notify($db,$recipient,'Новый ответ в теме «'.$t['title'].'»',link_to('thread',['id'=>$tid]),$u['id']);
        });alert('Ответ опубликован');back_to('thread',['id'=>$tid]);
    }
    if($action==='edit-post') {
        $body=inp('body',15000);demand(length_s($body)>=2 && length_s($body)<=12000,'Текст должен содержать 2–12000 символов');
        $tid=db_change(function (&$db) use($id,$u,$body){
            $p=findrow($db['posts'],$id);demand($p!==null && empty($p['deleted']),'Сообщение не найдено');$t=findrow($db['threads'],(int)$p['thread_id']);
            demand($t!==null && thread_visible($db,$t,$u),'Нет доступа');
            demand(staff($u)||((int)$p['author_id']===$u['id'] && time()-(int)$p['created']<=1800),'Редактировать свои сообщения можно в течение 30 минут');
            foreach($db['posts'] as &$item) if($item['id']===$id){$item['body']=$body;$item['updated']=time();break;}unset($item);
            return (int)$p['thread_id'];
        });alert('Сообщение обновлено');back_to('thread',['id'=>$tid]);
    }
    if($action==='edit-thread') {
        $title=inp('title',150);demand(length_s($title)>=5 && length_s($title)<=120,'Название: 5–120 символов');
        db_change(function (&$db) use($id,$u,$title){
            $t=findrow($db['threads'],$id);demand($t!==null && thread_visible($db,$t,$u),'Тема недоступна');
            demand(staff($u)||((int)$t['author_id']===$u['id']&&time()-(int)$t['created']<=1800),'Срок изменения названия истёк');
            foreach($db['threads'] as &$r)if($r['id']===$id){$r['title']=$title;break;}unset($r);
        });alert('Название изменено');back_to('thread',['id'=>$id]);
    }
    if(in_array($action,['subscribe','bookmark','react'],true)) {
        db_change(function (&$db) use($action,$id,$u){
            if($action==='react') {
                $p=findrow($db['posts'],$id);demand($p!==null&&empty($p['deleted']),'Сообщение не найдено');$t=findrow($db['threads'],(int)$p['thread_id']);demand($t && thread_visible($db,$t,$u),'Нет доступа');
                $rows=&$db['reactions'];$key='post_id';$type='Реакция';$destination=link_to('thread',['id'=>$p['thread_id']]);
            } else {
                $t=findrow($db['threads'],$id);demand($t && thread_visible($db,$t,$u),'Нет доступа');
                $rows=&$db[$action==='subscribe'?'subscriptions':'bookmarks'];$key='thread_id';$type='Закладка';$destination=link_to('thread',['id'=>$id]);
            }
            $found=false;
            foreach($rows as $n=>$r)if((int)$r[$key]===$id && (int)$r['user_id']===$u['id']){unset($rows[$n]);$found=true;break;}
            if(!$found){$rows[]=['user_id'=>$u['id'],$key=>$id];if($action==='react')notify($db,(int)$p['author_id'],'Ваше сообщение понравилось', $destination,$u['id']);}
            $rows=array_values($rows);
        });back_to('thread',['id'=>max(0,(int)($_POST['thread_id']??$id))]);
    }
    if($action==='report') {
        $reason=inp('reason',1600);demand(length_s($reason)>=10 && length_s($reason)<=1200,'Опишите нарушение (от 10 до 1200 символов)');
        db_change(function (&$db) use($id,$u,$reason){
            $p=findrow($db['posts'],$id);demand($p!==null&&empty($p['deleted']),'Сообщение не найдено');
            $t=findrow($db['threads'],(int)$p['thread_id']);demand($t&&thread_visible($db,$t,$u),'Недоступно');
            foreach($db['reports'] as $r) demand(!($r['post_id']===$id&&$r['reporter_id']===$u['id']&&$r['status']==='open'),'Вы уже сообщили о данном сообщении');
            limiter($db,'report:'.$u['id'],60);
            $db['reports'][]=['id'=>newid($db),'post_id'=>$id,'thread_id'=>$t['id'],'reporter_id'=>$u['id'],'reason'=>$reason,'created'=>time(),'status'=>'open','closed_by'=>null];
        });alert('Жалоба передана модерации');back_to('thread',['id'=>max(0,(int)($_POST['thread_id']??0))]);
    }
    if($action==='profile') {
        $bio=inp('bio',700);demand(length_s($bio)<=500,'Описание профиля слишком длинное');
        db_change(function (&$db) use($u,$bio){foreach($db['users'] as &$r)if($r['id']===$u['id']){$r['bio']=$bio;break;}unset($r);});
        alert('Профиль сохранён');back_to('account');
    }
    if($action==='password') {
        $old=(string)($_POST['old_password']??'');$pass=(string)($_POST['new_password']??'');
        demand(strlen($pass)>=10&&strlen($pass)<=200,'Новый пароль: от 10 до 200 символов');
        db_change(function (&$db) use($u,$old,$pass){foreach($db['users'] as &$r)if($r['id']===$u['id']){
            demand(password_verify($old,$r['pass']),'Текущий пароль неправильный');$r['pass']=password_hash($pass,PASSWORD_DEFAULT);break;
        }unset($r);});session_regenerate_id(true);alert('Пароль изменён');back_to('account');
    }
    if($action==='mark-read') {
        db_change(function (&$db) use($u){foreach($db['notifications'] as &$n) if($n['user_id']===$u['id'])$n['read']=true;unset($n);});back_to('notifications');
    }
    if(in_array($action,['lock','pin','delete-thread','restore-thread','move-thread','delete-post','restore-post'],true)) {
        need_staff($u);
        $return=db_change(function (&$db) use($id,$u,$action){
            if(in_array($action,['delete-post','restore-post'],true)) {
                $p=findrow($db['posts'],$id);demand($p!==null,'Сообщение не найдено');
                $first=topic_posts($db,(int)$p['thread_id'])[0]??null;
                demand(!$first || $first['id']!==$id,'Первое сообщение удаляется вместе с темой');
                foreach($db['posts'] as &$r)if($r['id']===$id){$r['deleted']=$action==='delete-post';break;}unset($r);
                audit($db,$u['id'],$action,'post '.$id);return (int)$p['thread_id'];
            }
            $t=findrow($db['threads'],$id);demand($t!==null,'Тема не найдена');
            foreach($db['threads'] as &$r)if($r['id']===$id){
                if($action==='lock')$r['locked']=!$r['locked'];
                if($action==='pin')$r['pinned']=!$r['pinned'];
                if($action==='delete-thread')$r['deleted']=true;
                if($action==='restore-thread')$r['deleted']=false;
                if($action==='move-thread'){$b=findrow($db['boards'],(int)($_POST['board_id']??0));demand($b!==null,'Раздел не найден');demand(can_move_thread_to_board($db,$t,$b),'Нельзя переносить приватную тему в публичный раздел');$r['board_id']=$b['id'];}
                break;
            }unset($r);
            audit($db,$u['id'],$action,'thread '.$id);return (int)$t['id'];
        });alert('Действие выполнено');back_to('thread',['id'=>$return]);
    }
    if($action==='close-report') {
        need_staff($u);
        db_change(function (&$db) use($id,$u){$found=false;foreach($db['reports'] as &$r)if($r['id']===$id){$r['status']='closed';$r['closed_by']=$u['id'];$found=true;break;}unset($r);demand($found,'Жалоба не найдена');audit($db,$u['id'],'close-report','report '.$id);});
        alert('Жалоба обработана');back_to('reports');
    }
    if($action==='user-role' || $action==='user-ban') {
        need_admin($u);demand($id!==$u['id'],'Нельзя изменять свой собственный аккаунт');
        $role=inp('role',20);if($action==='user-role')demand(in_array($role,['member','moderator','admin'],true),'Недопустимая роль');
        db_change(function (&$db)use($id,$u,$role,$action){
            $found=false;foreach($db['users'] as &$r)if($r['id']===$id){if($action==='user-role')$r['role']=$role;else $r['banned']=!$r['banned'];$found=true;break;}unset($r);
            demand($found,'Аккаунт не найден');audit($db,$u['id'],$action,'user '.$id);
        });alert('Пользователь обновлён');back_to('admin',['tab'=>'users']);
    }
    if($action==='save-settings') {
        need_admin($u);$values=[];
        foreach(['title'=>80,'subtitle'=>160,'announcement'=>1000,'vk'=>500,'tg'=>500,'site_url'=>500,'rules'=>5000,'footer'=>200] as $k=>$max)$values[$k]=inp($k,$max);
        foreach(['vk','tg','site_url'] as $k)demand(is_url($values[$k]),'Некорректная ссылка: '.$k);
        $values['registration']=isset($_POST['registration'])?'1':'0';
        db_change(function (&$db)use($values,$u){foreach($values as $k=>$v)$db['settings'][$k]=$v;audit($db,$u['id'],'settings','Общие настройки');});
        alert('Настройки обновлены');back_to('admin',['tab'=>'settings']);
    }
    if($action==='save-category') {
        need_admin($u);$title=inp('title',120);demand(length_s($title)>=3&&length_s($title)<=90,'Название категории: 3–90 символов');
        db_change(function (&$db)use($id,$u,$title){
            if(!$id){$db['categories'][]=['id'=>newid($db),'title'=>$title,'description'=>'','sort'=>count($db['categories'])*10+10];}
            else{$found=false;foreach($db['categories'] as &$r)if($r['id']===$id){$r['title']=$title;$found=true;break;}unset($r);demand($found,'Категория не найдена');}
            audit($db,$u['id'],'category','category '.$id.' '.$title);
        });alert('Категория сохранена');back_to('admin',['tab'=>'boards']);
    }
    if($action==='save-board') {
        need_admin($u);$title=inp('title',110);$desc=inp('description',300);$cat=(int)($_POST['category_id']??0);$private=isset($_POST['private']);$staffOnly=isset($_POST['only_staff']);
        demand(length_s($title)>=3&&length_s($title)<=90,'Название раздела: 3–90 символов');
        db_change(function (&$db)use($id,$u,$title,$desc,$cat,$private,$staffOnly){
            demand(findrow($db['categories'],$cat)!==null,'Категория не найдена');
            if(!$id)$db['boards'][]=['id'=>newid($db),'category_id'=>$cat,'title'=>$title,'description'=>$desc,'icon'=>'chat','private'=>$private,'only_staff'=>$staffOnly,'sort'=>count($db['boards'])+1];
            else{demand(can_change_board_privacy($db,$id,$private),'Нельзя отключить приватность раздела с темами');$found=false;foreach($db['boards'] as &$r)if($r['id']===$id){$r['title']=$title;$r['description']=$desc;$r['category_id']=$cat;$r['private']=$private;$r['only_staff']=$staffOnly;$found=true;break;}unset($r);demand($found,'Раздел не найден');}
            audit($db,$u['id'],'board','board '.$id.' '.$title);
        });alert('Раздел сохранён');back_to('admin',['tab'=>'boards']);
    }
    if($action==='delete-board') {
        need_admin($u);db_change(function (&$db)use($u,$id){
            foreach($db['threads'] as $t)demand($t['board_id']!==$id,'Нельзя удалить раздел с темами');
            $before=count($db['boards']);$db['boards']=array_values(array_filter($db['boards'],fn($b)=>$b['id']!==$id));
            demand(count($db['boards'])<$before,'Раздел не найден');audit($db,$u['id'],'delete-board','board '.$id);
        });alert('Пустой раздел удалён');back_to('admin',['tab'=>'boards']);
    }
    throw new DomainException('Неизвестное действие');
} catch(DomainException $ex) {alert($ex->getMessage(),'error');back_to($page,$pageId?['id'=>$pageId]:[]);}
catch(Throwable $ex) {error_log('[VELTOR FORUM] '.$ex->getMessage());alert('Не удалось выполнить действие. Проверьте настройки хостинга.','error');back_to($page,$pageId?['id'=>$pageId]:[]);}
