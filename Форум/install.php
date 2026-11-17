<?php
declare(strict_types=1);
require __DIR__.'/app/core.php';
boot();
if(is_file(DATA_FILE)){header('Location: index.php',true,302);exit;}
$err='';
if(($_SERVER['REQUEST_METHOD']??'GET')==='POST'){
    verify_csrf();
    $name=inp('username',32);$email=strtolower(inp('email',180));$pass=(string)($_POST['password']??'');$confirm=(string)($_POST['confirm']??'');
    try{
        demand((bool)preg_match('/^[\p{L}\p{N}_-]{3,24}$/u',$name),'Логин: 3–24 буквы, цифры, дефисы или подчёркивания');
        demand((bool)filter_var($email,FILTER_VALIDATE_EMAIL)&&strlen($email)<=180,'Проверьте почту');
        demand(strlen($pass)>=12&&strlen($pass)<=200,'Для администратора требуется пароль не короче 12 символов');
        demand($pass===$confirm,'Пароли не совпадают');
        db_change(function (&$db)use($name,$email,$pass){demand($db===null,'Форум уже установлен');$db=init_db($name,$pass,$email);});
        session_login(1);alert('Форум установлен! Добро пожаловать, администратор.');header('Location: index.php?p=admin',true,303);exit;
    }catch(DomainException $e){$err=$e->getMessage();}catch(Throwable $e){error_log('[VELTOR SETUP] '.$e->getMessage());$err='Ошибка записи данных. Проверьте права доступа к папке storage/.';}
}
?><!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#0c1119"><title>Установка — VELTOR ROLEPLAY</title><link rel="stylesheet" href="assets/css/forum.css"></head><body class="install-body"><main class="install-container"><div class="install-hero"><span class="identity-icon">V</span><strong>VELTOR ROLEPLAY</strong><small>ПЕРВОНАЧАЛЬНАЯ УСТАНОВКА</small></div><section class="form-card"><span class="overline">ШАГ 01 / 01</span><h1>Настройка форума</h1><p>Создайте администратора. После установки появятся стандартные разделы VELTOR, темы с правилами и панель управления.</p><?php if($err):?><div class="alert error"><?=h($err)?></div><?php endif;?><?php if(PHP_VERSION_ID<80100):?><div class="alert error">Нужен PHP 8.1 или выше.</div><?php elseif(!is_writable(__DIR__.'/storage')):?><div class="alert error">Каталог storage/ должен быть доступен на запись для PHP.</div><?php endif;?><form class="form" method="post" action="install.php"><?=field_csrf()?><label>Имя администратора<input name="username" autocomplete="username" required minlength="3" maxlength="24" placeholder="Admin"></label><label>Электронная почта<input name="email" type="email" autocomplete="email" required maxlength="180" placeholder="admin@example.com"></label><label>Надёжный пароль<input type="password" name="password" autocomplete="new-password" minlength="12" maxlength="200" required></label><label>Повтор пароля<input type="password" name="confirm" autocomplete="new-password" minlength="12" maxlength="200" required></label><button type="submit" class="btn accent">Создать форум →</button></form><div class="install-hint">Требования: PHP 8.1+, расширения JSON и OpenSSL, возможность записи в storage/. MySQL не требуется.<br><br>Для Nginx обязательно запретите HTTP-доступ к <code>/storage/</code> и <code>/app/</code>. Рекомендуется HTTPS.</div></section></main></body></html>
