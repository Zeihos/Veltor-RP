<?php
declare(strict_types=1);
require __DIR__.'/app/core.php';
boot();
if (!is_file(DATA_FILE)) { header('Location: install.php'); exit; }
require __DIR__.'/app/actions.php';
require __DIR__.'/app/pages.php';
