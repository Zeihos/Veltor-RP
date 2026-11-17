<?php
declare(strict_types=1);
require __DIR__.'/app/core.php'; boot();
if (is_file(DATA_FILE)) {header('Location: index.php',true,302);exit;}
$error='';
if (is_post()) {
  verify_csrf();
  try {
    $name=val('username',24);$pass=(string)($_POST['password']??'');$confirm=(string)($_POST['confirm']??'');
    ensure(preg_match('/^[a-zA-Z0-9_]{3,24}$/D',$name)===1,'Логин: 3–24 латинские буквы, цифры или _.');
    check_password($pass);ensure(hash_equals($pass,$confirm),'Пароли не совпадают.');
    store_write(function (&$db) use ($name,$pass) {ensure($db===null,'Сайт уже установлен.');$db=initial_data($name,$pass);});
    start_session(1);header('Location: index.php?p=admin',true,303);exit;
  } catch(Throwable $ex){$error=$ex->getMessage();}
}
?><!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Установка VELTOR ROLEPLAY</title><link rel="stylesheet" href="assets/css/style.css"><link rel="stylesheet" href="assets/css/app.css"></head><body><main class="container" style="max-width:620px;padding:8vh 0"><div class="panel"><div class="eyebrow">VELTOR ROLEPLAY — <?=VELTOR_VERSION?></div><h1 class="title">УСТАНОВКА <span class="grad">САЙТА</span></h1><p class="muted">Настройте администратора. База данных будет создана автоматически, MySQL и дополнительные PHP-модули не требуются. После установки эта страница заблокируется.</p><?php if($error):?><div class="flash error"><?=e($error)?></div><?php endif;?><form method="post" class="form-stack"><?=csrf_field()?><?php field('username','Логин администратора','','text',true,'minlength="3" maxlength="24" autocomplete="username"');?><label class="field"><span>Пароль (не менее 10 символов)</span><input name="password" type="password" required minlength="10" autocomplete="new-password"></label><label class="field"><span>Повторите пароль</span><input name="confirm" type="password" required minlength="10" autocomplete="new-password"></label><?php button('Установить и открыть панель');?></form><p class="muted small">Требуется PHP 8.1+ и возможность записи в папку <code>storage</code>. Для публичного сайта используйте HTTPS.</p></div></main></body></html>
