<?php
// Sandbox preview traffic so studio previews never write to data/traffic.sqlite.
$studioTmpTraffic = dirname(__DIR__) . '/state/tmp-traffic';
if (!is_dir($studioTmpTraffic)) {
    @mkdir($studioTmpTraffic, 0700, true);
}
if (!defined('TRAFFIC_DATA_DIR_OVERRIDE')) {
    define('TRAFFIC_DATA_DIR_OVERRIDE', $studioTmpTraffic);
}
$studioDocRoot = isset($_SERVER['DOCUMENT_ROOT']) ? rtrim((string) $_SERVER['DOCUMENT_ROOT'], "/\\") : '';
$studioConfigFile = dirname(__DIR__) . '/state/config.json';
$studioExcludeRel = '/includes/traffic_exclude.php';
if (is_file($studioConfigFile)) {
    $studioCfg = json_decode((string) file_get_contents($studioConfigFile), true);
    if (is_array($studioCfg) && !empty($studioCfg['trafficExcludeInclude']) && is_string($studioCfg['trafficExcludeInclude'])) {
        $studioExcludeRel = '/' . ltrim($studioCfg['trafficExcludeInclude'], '/');
    }
}
$studioExcludeFile = $studioDocRoot !== '' ? $studioDocRoot . $studioExcludeRel : '';
if ($studioExcludeFile === '' || !is_file($studioExcludeFile)) {
    $studioExcludeFile = dirname(__DIR__, 2) . $studioExcludeRel;
}
if (is_file($studioExcludeFile)) {
    require_once $studioExcludeFile;
    if (function_exists('traffic_exclude_token') && function_exists('traffic_exclude_cookie_name')) {
        $_COOKIE[traffic_exclude_cookie_name()] = traffic_exclude_token(time() + 3600);
    }
}
if (!empty($_SERVER['HTTP_X_PREVIEW_DOCUMENT_URI'])) {
    $docUri = (string) $_SERVER['HTTP_X_PREVIEW_DOCUMENT_URI'];
    $_SERVER['DOCUMENT_URI'] = $docUri;
    $_SERVER['REDIRECT_URL'] = $docUri;
    $_SERVER['REQUEST_URI'] = $docUri;
}

function studio_draft_requested() {
    if (!empty($_GET['__draft']) || !empty($_GET['__preview_draft'])) return true;
    $header = $_SERVER['HTTP_X_PREVIEW_DRAFT'] ?? '';
    return $header === '1' || strcasecmp($header, 'true') === 0;
}

function studio_draft_file() {
    $candidates = [];
    $root = isset($_SERVER['DOCUMENT_ROOT']) ? rtrim((string) $_SERVER['DOCUMENT_ROOT'], "/\\") : '';
    $candidates[] = dirname(__DIR__) . '/state/draft.json';
    if ($root !== '') $candidates[] = $root . '/studio/state/draft.json';
    foreach ($candidates as $path) {
        if (is_file($path)) return $path;
    }
    return '';
}

function studio_public_script() {
    $name = isset($_SERVER['SCRIPT_NAME']) ? str_replace('\\', '/', (string) $_SERVER['SCRIPT_NAME']) : '';
    if ($name !== '' && $name[0] === '/') return $name;
    $script = isset($_SERVER['SCRIPT_FILENAME']) ? str_replace('\\', '/', (string) $_SERVER['SCRIPT_FILENAME']) : '';
    $root = isset($_SERVER['DOCUMENT_ROOT']) ? rtrim(str_replace('\\', '/', (string) $_SERVER['DOCUMENT_ROOT']), '/') : '';
    if ($script !== '' && $root !== '' && strncmp($script, $root, strlen($root)) === 0) {
        $rel = substr($script, strlen($root));
        if ($rel === '' || $rel[0] !== '/') $rel = '/' . $rel;
        return $rel;
    }
    return '';
}

function studio_strip_draft_query() {
    unset($_GET['__draft'], $_GET['__preview_draft'], $_REQUEST['__draft'], $_REQUEST['__preview_draft']);
    $query = isset($_SERVER['QUERY_STRING']) ? (string) $_SERVER['QUERY_STRING'] : '';
    $kept = [];
    foreach ($query === '' ? [] : explode('&', $query) as $part) {
        $name = rawurldecode(explode('=', $part, 2)[0]);
        if ($name === '__draft' || $name === '__preview_draft') continue;
        $kept[] = $part;
    }
    $_SERVER['QUERY_STRING'] = implode('&', $kept);
    $uri = isset($_SERVER['REQUEST_URI']) ? (string) $_SERVER['REQUEST_URI'] : '';
    $path = strtok($uri, '?');
    if ($path === false || $path === '') $path = $_SERVER['SCRIPT_NAME'] ?? '/';
    $_SERVER['REQUEST_URI'] = $_SERVER['QUERY_STRING'] === '' ? $path : ($path . '?' . $_SERVER['QUERY_STRING']);
}

function studio_staged_images(array $images) {
    $rows = [];
    foreach ($images as $image) {
        if (!is_array($image)) continue;
        $name = (string) ($image['name'] ?? $image['image_name'] ?? '');
        if ($name === '') continue;
        $rows[] = [
            'image_name' => $name,
            'caption' => (string) ($image['caption'] ?? ''),
        ];
    }
    return $rows;
}

function studio_prepare_draft(array $draft) {
    $script = isset($_SERVER['SCRIPT_FILENAME']) ? (string) $_SERVER['SCRIPT_FILENAME'] : '';
    if ($script === '' || !is_file($script)) return '';
    $code = isset($draft['content']) ? (string) $draft['content'] : '';
    if ($code === '') $code = (string) file_get_contents($script);
    if ($code === '') return '';
    $dir = dirname($script);
    if (isset($draft['images']) && is_array($draft['images'])) {
        $export = var_export(studio_staged_images($draft['images']), true);
        if (preg_match('/\$image_data\s*=\s*read_image_data\s*\([^;]*\)\s*;/', $code, $match, PREG_OFFSET_CAPTURE)) {
            $at = $match[0][1];
            $code = substr($code, 0, $at) . '$image_data = ' . $export . ';' . substr($code, $at + strlen($match[0][0]));
        }
    }
    $code = str_replace('__DIR__', var_export($dir, true), $code);
    $code = str_replace('__FILE__', var_export($script, true), $code);
    $code = preg_replace('/\A\s*<\?(?:php)?\s*/', '', $code, 1);
    return is_string($code) ? $code : '';
}

if (studio_draft_requested()) {
    $studioPublic = studio_public_script();
    $studioDraftFile = studio_draft_file();
    if ($studioPublic !== '' && $studioDraftFile !== '') {
        $studioRaw = file_get_contents($studioDraftFile);
        $studioData = is_string($studioRaw) ? json_decode($studioRaw, true) : null;
        if (is_array($studioData) && isset($studioData['drafts'][$studioPublic]) && is_array($studioData['drafts'][$studioPublic])) {
            $studioCode = studio_prepare_draft($studioData['drafts'][$studioPublic]);
            if ($studioCode !== '') {
                studio_strip_draft_query();
                eval($studioCode);
                exit;
            }
        }
    }
}
