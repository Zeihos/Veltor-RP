<?php
declare(strict_types=1);
require __DIR__.'/app/core.php';
boot();
if (!is_file(DATA_FILE)) {header('Location: install.php',true,302);exit;}
try { $db=store_read(); } catch(Throwable $e) {http_response_code(503);exit('Не удалось загрузить данные сайта. Обратитесь к администратору.');}
if ($db===null){header('Location: install.php',true,302);exit;}
$user=logged_user($db);
$page=(string)($_GET['p']??'home');
$allowed=['home','play','servers','news','article','store','forum','topic','contacts','privacy','login','register','account','orders','tickets','ticket','admin','admin-news','admin-servers','admin-store','admin-orders','admin-users','admin-forum','admin-tickets','admin-messages','admin-settings'];
if (!in_array($page,$allowed,true)) {$page='notfound';http_response_code(404);}
if (str_starts_with($page,'admin')) must_admin($user);
$action=(string)($_POST['action']??'');
if (is_post()) {
  verify_csrf();
  $back=$page;$args=[];
  try {
    if ($action==='logout') {$_SESSION=[];session_regenerate_id(true);flash('Вы вышли из аккаунта.');go('home');}
    $loginActions=['login','register'];
    $adminActions=['save_news','delete_news','save_server','delete_server','save_product','delete_product','order_status','user_ban','user_role','user_password','delete_topic','topic_control','ticket_status','message_delete','save_settings'];
    if (in_array($action,$adminActions,true)) must_admin($user);
    if (!in_array($action,array_merge($loginActions,['contact']),true) && !in_array($action,$adminActions,true)) must_login($user);
    $actorId=(int)($user['id']??0);
    $result=store_write(function (&$d) use ($action,$actorId,&$back,&$args) {
      ensure($d!==null,'Сайт не установлен.');
      $actor=one($d,'users',$actorId);
      if ($actorId) ensure($actor!==null && empty($actor['banned']),'Доступ к аккаунту ограничен.');
      $admin=$actor && $actor['role']==='admin';
      $id=(int)($_POST['id']??0);
      switch ($action) {
        case 'register':
          ensure(!$actor,'Вы уже авторизованы.');
          ensure(setting($d,'registration_open')==='1','Регистрация временно закрыта.');
          $name=val('username',24);$email=val('email',180);$pass=(string)($_POST['password']??'');$pass2=(string)($_POST['confirm']??'');
          ensure(preg_match('/^[A-Za-z0-9_]{3,24}$/D',$name)===1,'Логин: от 3 до 24 символов латиницей, цифрами и _.');
          ensure($email==='' || (strlen($email)<180 && filter_var($email,FILTER_VALIDATE_EMAIL)),'Некорректная почта.');
          check_password($pass);ensure(hash_equals($pass,$pass2),'Пароли не совпадают.');
          ensure(!user_by_name($d,$name),'Этот логин уже занят.');
          foreach($d['users'] as $u) ensure($email==='' || $u['email']==='' || strcasecmp($u['email'],$email)!==0,'Эта почта уже занята.');
          $uid=new_id($d);$d['users'][]=['id'=>$uid,'username'=>$name,'email'=>$email,'password'=>password_hash($pass,PASSWORD_DEFAULT),'role'=>'user','created'=>time(),'banned'=>false,'nickname'=>''];
          $back='account';return ['id'=>$uid,'message'=>'Аккаунт создан. Добро пожаловать!'];
        case 'login':
          ensure(!$actor,'Вы уже авторизованы.');
          $name=val('username',100);$pass=(string)($_POST['password']??'');
          $key=hash('sha256',strtolower($name).'|'.($_SERVER['REMOTE_ADDR']??''));throttle($d,$key);
          $found=user_by_name($d,$name);
          if (!$found || !empty($found['banned']) || !password_verify($pass,$found['password'])) { $back='login'; return ['message'=>'Неверный логин или пароль либо доступ ограничен.','error'=>true]; }
          unset($d['attempts'][$key]);$back='account';return ['id'=>$found['id'],'message'=>'Вы успешно вошли.'];
        case 'profile':
          $nick=val('nickname',24);$email=val('email',180);
          ensure($nick===''||preg_match('/^[A-Za-z0-9_\-]{3,24}$/D',$nick)===1,'Игровой ник: латиница, цифры, _ или -, от 3 до 24 символов.');
          ensure($email===''||filter_var($email,FILTER_VALIDATE_EMAIL),'Некорректная почта.');
          foreach($d['users'] as $u) if($u['id']!==$actorId) ensure($email===''||$u['email']===''||strcasecmp($u['email'],$email)!==0,'Эта почта уже используется.');
          foreach($d['users'] as &$u) if($u['id']===$actorId){$u['nickname']=$nick;$u['email']=$email;break;}unset($u);
          $back='account';return ['message'=>'Профиль обновлён.'];
        case 'password':
          $old=(string)($_POST['old_password']??'');$new=(string)($_POST['new_password']??'');
          ensure(password_verify($old,$actor['password']),'Неверный текущий пароль.');check_password($new);
          foreach($d['users'] as &$u) if($u['id']===$actorId){$u['password']=password_hash($new,PASSWORD_DEFAULT);break;}unset($u);
          $back='account';return ['message'=>'Пароль изменён.'];
        case 'contact':
          $name=limited(val('name',80),80);$email=val('email',180);$subject=limited(val('subject',150),150);$body=val('body',4000);
          ensure(val('website',100)==='','Спам-фильтр: запрос отклонён.');
          ensure(strlen($name)>=2 && strlen($subject)>=3 && strlen($body)>=10,'Заполните все поля сообщения.');
          ensure($email===''||filter_var($email,FILTER_VALIDATE_EMAIL),'Некорректный email.');
          $key='contact:'.hash('sha256',(string)($_SERVER['REMOTE_ADDR']??''));
          $last=$d['attempts'][$key]['time']??0;ensure($last<time()-60,'Следующее сообщение можно отправить через минуту.');
          $d['attempts'][$key]=['count'=>1,'time'=>time()];
          $d['messages'][]=['id'=>new_id($d),'name'=>$name,'email'=>$email,'subject'=>$subject,'body'=>$body,'created'=>time(),'read'=>false];
          $back='contacts';return ['message'=>'Сообщение отправлено администрации сайта.'];
        case 'order':
          rate_limit_write($d,'order:'.$actorId,25);
          ensure(setting($d,'shop_open')==='1','Приём заявок сейчас отключён.');
          $product=one($d,'products',$id);ensure($product && !empty($product['active']),'Товар недоступен.');
          $nick=val('nickname',24);ensure(preg_match('/^[A-Za-z0-9_\-]{3,24}$/D',$nick)===1,'Укажите корректный игровой ник.');
          $d['orders'][]=['id'=>new_id($d),'user_id'=>$actorId,'product_id'=>$id,'product_name'=>$product['name'],'price'=>$product['price'],'nickname'=>$nick,'status'=>'new','created'=>time(),'note'=>''];
          $back='orders';return ['message'=>'Заявка создана. Это не оплата: администрация обработает её вручную.'];
        case 'new_topic':
          rate_limit_write($d,'topic:'.$actorId,30);
          $title=limited(val('title',140),140);$body=val('body',6000);
          ensure(strlen($title)>=5 && strlen($body)>=10,'Тема должна содержать заголовок и сообщение.');
          $tid=new_id($d);$d['topics'][]=['id'=>$tid,'user_id'=>$actorId,'title'=>$title,'created'=>time(),'updated'=>time(),'locked'=>false,'pinned'=>false];
          $d['posts'][]=['id'=>new_id($d),'topic_id'=>$tid,'user_id'=>$actorId,'body'=>$body,'created'=>time()];
          $back='topic';$args=['id'=>$tid];return ['message'=>'Тема опубликована.'];
        case 'reply_topic':
          rate_limit_write($d,'reply:'.$actorId,10);
          $topic=one($d,'topics',$id);ensure($topic && !$topic['locked'],'Тема не найдена или закрыта.');
          $body=val('body',6000);ensure(strlen($body)>=2,'Напишите сообщение.');
          $d['posts'][]=['id'=>new_id($d),'topic_id'=>$id,'user_id'=>$actorId,'body'=>$body,'created'=>time()];
          foreach($d['topics'] as &$t) if($t['id']===$id){$t['updated']=time();break;}unset($t);
          $back='topic';$args=['id'=>$id];return ['message'=>'Ответ опубликован.'];
        case 'new_ticket':
          rate_limit_write($d,'ticket:'.$actorId,60);
          $subject=limited(val('subject',140),140);$body=val('body',5000);
          ensure(strlen($subject)>=5 && strlen($body)>=10,'Заполните тему и описание обращения.');
          $tid=new_id($d);$d['tickets'][]=['id'=>$tid,'user_id'=>$actorId,'subject'=>$subject,'status'=>'open','created'=>time(),'updated'=>time()];
          $d['ticket_replies'][]=['id'=>new_id($d),'ticket_id'=>$tid,'user_id'=>$actorId,'body'=>$body,'created'=>time()];
          $back='ticket';$args=['id'=>$tid];return ['message'=>'Обращение отправлено.'];
        case 'reply_ticket':
          $t=one($d,'tickets',$id);ensure($t && ($t['user_id']===$actorId||$admin),'Обращение недоступно.');
          ensure($t['status']==='open','Обращение закрыто.');$body=val('body',5000);ensure(strlen($body)>=2,'Ответ не может быть пустым.');
          $d['ticket_replies'][]=['id'=>new_id($d),'ticket_id'=>$id,'user_id'=>$actorId,'body'=>$body,'created'=>time()];
          foreach($d['tickets'] as &$v) if($v['id']===$id){$v['updated']=time();break;}unset($v);
          $back=$admin?'admin-tickets':'ticket';$args=$admin?[]:['id'=>$id];return ['message'=>'Ответ добавлен.'];
        case 'save_news':
          ensure($admin,'Доступ запрещён.');$title=limited(val('title',160),160);$body=val('body',15000);
          ensure(strlen($title)>=5 && strlen($body)>=10,'Заполните заголовок и текст новости.');
          $entry=['title'=>$title,'body'=>$body,'published'=>isset($_POST['published']),'pinned'=>isset($_POST['pinned']),'updated'=>time()];
          if($id){ensure(one($d,'news',$id)!==null,'Новость не найдена.');foreach($d['news'] as &$n) if($n['id']===$id){$n=array_merge($n,$entry);break;}unset($n);}else{$d['news'][]=array_merge(['id'=>new_id($d),'created'=>time()],$entry);}
          $back='admin-news';return ['message'=>'Новость сохранена.'];
        case 'delete_news':case 'delete_server':case 'delete_product':
          ensure($admin,'Доступ запрещён.');$table=['delete_news'=>'news','delete_server'=>'servers','delete_product'=>'products'][$action];
          ensure(one($d,$table,$id)!==null,'Запись не найдена.');$d[$table]=array_values(array_filter($d[$table],fn($i)=>$i['id']!==$id));
          $back=['news'=>'admin-news','servers'=>'admin-servers','products'=>'admin-store'][$table];return ['message'=>'Запись удалена.'];
        case 'save_server':
          ensure($admin,'Доступ запрещён.');$name=limited(val('name',100),100);$host=val('host',250);$port=(int)($_POST['port']??7777);
          ensure(strlen($name)>=3,'Укажите название сервера.');ensure($host===''||preg_match('/^[a-zA-Z0-9.\-]{1,250}$/D',$host)===1,'Некорректный адрес сервера.');
          ensure($port>=1 && $port<=65535,'Порт должен быть от 1 до 65535.');
          $entry=['name'=>$name,'host'=>$host,'port'=>$port,'status'=>in_array(val('status',20),['preparing','online','maintenance'],true)?val('status',20):'preparing','description'=>limited(val('description',500),500),'updated'=>time()];
          if($id){ensure(one($d,'servers',$id)!==null,'Сервер не найден.');foreach($d['servers'] as &$s)if($s['id']===$id){$s=array_merge($s,$entry);break;}unset($s);}else{$d['servers'][]=array_merge(['id'=>new_id($d),'created'=>time()],$entry);}
          $back='admin-servers';return ['message'=>'Сервер сохранён. Статус указывается администратором, а не мониторингом.'];
        case 'save_product':
          ensure($admin,'Доступ запрещён.');$name=limited(val('name',100),100);$description=limited(val('description',900),900);$price=val('price',20);
          ensure(strlen($name)>=3 && strlen($description)>=5,'Заполните название и описание.');
          ensure(preg_match('/^\d{1,8}([.,]\d{1,2})?$/D',$price)===1,'Некорректная цена.');$price=number_format((float)str_replace(',','.',$price),2,'.','');
          $entry=['name'=>$name,'description'=>$description,'price'=>$price,'active'=>isset($_POST['active']),'updated'=>time()];
          if($id){ensure(one($d,'products',$id)!==null,'Товар не найден.');foreach($d['products'] as &$v) if($v['id']===$id){$v=array_merge($v,$entry);break;}unset($v);}else{$d['products'][]=array_merge(['id'=>new_id($d),'created'=>time()],$entry);}
          $back='admin-store';return ['message'=>'Товар сохранён.'];
        case 'order_status':
          ensure($admin,'Доступ запрещён.');$status=val('status',20);ensure(in_array($status,['new','processing','completed','cancelled'],true),'Некорректный статус.');
          ensure(one($d,'orders',$id)!==null,'Заказ не найден.');
          foreach($d['orders'] as &$o)if($o['id']===$id){$o['status']=$status;$o['note']=limited(val('note',500),500);break;}unset($o);
          $back='admin-orders';return ['message'=>'Заявка обновлена.'];
        case 'user_ban':
          ensure($admin && $id!==$actorId,'Невозможно заблокировать себя.');ensure(one($d,'users',$id)!==null,'Пользователь не найден.');
          foreach($d['users'] as &$u)if($u['id']===$id){ensure($u['role']!=='admin','Нельзя блокировать администратора.');$u['banned']=!$u['banned'];break;}unset($u);
          $back='admin-users';return ['message'=>'Статус пользователя обновлён.'];
        case 'user_role':
          ensure($admin && $id!==$actorId,'Невозможно изменить свою роль.');$role=val('role',20);ensure(in_array($role,['admin','user'],true),'Некорректная роль.');
          ensure(one($d,'users',$id)!==null,'Пользователь не найден.');
          foreach($d['users'] as &$u)if($u['id']===$id){$u['role']=$role;$u['banned']=false;break;}unset($u);
          $back='admin-users';return ['message'=>'Роль изменена.'];
        case 'user_password':
          ensure($admin,'Доступ запрещён.');$pass=(string)($_POST['new_password']??'');check_password($pass);
          ensure(one($d,'users',$id)!==null,'Пользователь не найден.');foreach($d['users'] as &$u)if($u['id']===$id){$u['password']=password_hash($pass,PASSWORD_DEFAULT);break;}unset($u);
          $back='admin-users';return ['message'=>'Пароль пользователя изменён. Передайте его безопасным способом.'];
        case 'topic_control':
          ensure($admin,'Доступ запрещён.');$v=val('field',20);ensure(in_array($v,['locked','pinned'],true),'Некорректное действие.');
          ensure(one($d,'topics',$id)!==null,'Тема не найдена.');foreach($d['topics'] as &$t)if($t['id']===$id){$t[$v]=!$t[$v];break;}unset($t);
          $back='admin-forum';return ['message'=>'Настройки темы изменены.'];
        case 'delete_topic':
          ensure($admin,'Доступ запрещён.');$d['topics']=array_values(array_filter($d['topics'],fn($t)=>$t['id']!==$id));$d['posts']=array_values(array_filter($d['posts'],fn($p)=>$p['topic_id']!==$id));
          $back='admin-forum';return ['message'=>'Тема и ответы удалены.'];
        case 'ticket_status':
          ensure($admin,'Доступ запрещён.');$status=val('status',20);ensure(in_array($status,['open','closed'],true),'Некорректный статус.');
          ensure(one($d,'tickets',$id)!==null,'Обращение не найдено.');foreach($d['tickets'] as &$t)if($t['id']===$id){$t['status']=$status;break;}unset($t);
          $back='admin-tickets';return ['message'=>'Статус обращения обновлён.'];
        case 'message_delete':
          ensure($admin,'Доступ запрещён.');$d['messages']=array_values(array_filter($d['messages'],fn($m)=>$m['id']!==$id));
          $back='admin-messages';return ['message'=>'Сообщение удалено.'];
        case 'save_settings':
          ensure($admin,'Доступ запрещён.');
          foreach(['site_name'=>80,'tagline'=>120,'description'=>300,'announcement'=>600,'start_date'=>80,'support_email'=>180,'vk_url'=>500,'tg_url'=>500,'launcher_url'=>500] as $k=>$length){$v=limited(val($k,$length),$length);
            if(in_array($k,['vk_url','tg_url','launcher_url'],true)) ensure(is_valid_http_url($v),'Проверьте URL поля '.$k.'.');
            if($k==='support_email')ensure($v===''||filter_var($v,FILTER_VALIDATE_EMAIL),'Некорректный адрес поддержки.');
            $d['settings'][$k]=$v;
          }
          ensure(setting($d,'site_name')!=='','Название сайта не может быть пустым.');
          $d['settings']['registration_open']=isset($_POST['registration_open'])?'1':'0';
          $d['settings']['shop_open']=isset($_POST['shop_open'])?'1':'0';
          $back='admin-settings';return ['message'=>'Настройки сохранены.'];
        default: throw new DomainException('Неизвестное действие формы.');
      }
    });
    if (in_array($action,['login','register'],true) && isset($result['id']))start_session((int)$result['id']);
    flash($result['message']??'Готово.',!empty($result['error'])?'error':'ok');go($back,$args);
  } catch(DomainException $e) {flash($e->getMessage(),'error');go($page,isset($_GET['id'])?['id'=>(int)$_GET['id']]:[]);} catch(Throwable $e) {error_log('VELTOR: '.$e->getMessage());flash('Не удалось выполнить операцию. Проверьте настройки хостинга.','error');go($page);}
}
require __DIR__.'/app/pages.php';
