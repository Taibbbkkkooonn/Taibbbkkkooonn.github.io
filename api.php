<?php
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    exit;
}

date_default_timezone_set('Asia/Ho_Chi_Minh');

$file = __DIR__ . '/history.json';

function http_get_json($url, $timeout_sec = 10) {
    $ctx = stream_context_create([
        'http' => [
            'method'  => 'GET',
            'timeout' => $timeout_sec,
            'header'  => "User-Agent: iotvisionsolution-smartfarm/1.0\r\n"
        ]
    ]);
    $raw = @file_get_contents($url, false, $ctx);
    if ($raw === false) return null;
    $j = json_decode($raw, true);
    if (!is_array($j)) return null;
    return $j;
}

function parse_time_to_ts($time_str) {
    $ts = strtotime($time_str);
    if ($ts === false) return null;
    return $ts;
}

function build_weather_open_meteo($lat, $lon, $days, $prob_min) {
    $tz = 'Asia/Ho_Chi_Minh';
    $days = intval($days);
    if ($days < 1) $days = 1;
    if ($days > 14) $days = 14;

    $prob_min = intval($prob_min);
    if ($prob_min < 0) $prob_min = 0;
    if ($prob_min > 100) $prob_min = 100;

    $url = 'https://api.open-meteo.com/v1/forecast'
        . '?latitude=' . urlencode($lat)
        . '&longitude=' . urlencode($lon)
        . '&timezone=' . urlencode($tz)
        . '&hourly=precipitation,precipitation_probability,rain'
        . '&daily=precipitation_sum,precipitation_probability_max'
        . '&forecast_days=' . urlencode($days);

    $j = http_get_json($url, 10);
    if (!$j) return null;

    $now_ts  = time();
    $today_d = date('Y-m-d');

    $hourly_time = isset($j['hourly']['time']) ? $j['hourly']['time'] : [];
    $hourly_prec = isset($j['hourly']['precipitation']) ? $j['hourly']['precipitation'] : [];
    $hourly_prob = isset($j['hourly']['precipitation_probability']) ? $j['hourly']['precipitation_probability'] : [];
    $hourly_rain = isset($j['hourly']['rain']) ? $j['hourly']['rain'] : [];

    $daily_time = isset($j['daily']['time']) ? $j['daily']['time'] : [];
    $daily_sum  = isset($j['daily']['precipitation_sum']) ? $j['daily']['precipitation_sum'] : [];
    $daily_pmax = isset($j['daily']['precipitation_probability_max']) ? $j['daily']['precipitation_probability_max'] : [];

    $next_rain = false;
    $next_rain_time = null;
    $next_rain_date = null;
    $next_rain_in_minutes = null;

    $today_will_rain = false;
    $today_next_rain_time = null;

    $count_hour = min(count($hourly_time), count($hourly_prec), count($hourly_prob), count($hourly_rain));
    for ($i = 0; $i < $count_hour; $i++) {
        $t_str = $hourly_time[$i];
        $t_ts = parse_time_to_ts($t_str);
        if ($t_ts === null) continue;

        $prec = floatval($hourly_prec[$i]);
        $rain = floatval($hourly_rain[$i]);
        $prob = intval($hourly_prob[$i]);

        $is_rain = ($rain > 0.0) || ($prec > 0.0);

        if (date('Y-m-d', $t_ts) === $today_d && $is_rain) {
            $today_will_rain = true;
            if ($today_next_rain_time === null && $t_ts >= $now_ts) {
                $today_next_rain_time = date('H:i', $t_ts);
            }
        }

        if ($t_ts < $now_ts) continue;

        if ($is_rain && $prob >= $prob_min) {
            $next_rain = true;
            $next_rain_time = date('H:i', $t_ts);
            $next_rain_date = date('Y-m-d', $t_ts);
            $next_rain_in_minutes = intval(($t_ts - $now_ts) / 60);
            break;
        }
    }

    $daily = [];
    $count_day = min(count($daily_time), count($daily_sum), count($daily_pmax));
    for ($i = 0; $i < $count_day; $i++) {
        $d = $daily_time[$i];
        $sum = floatval($daily_sum[$i]);
        $pmax = intval($daily_pmax[$i]);
        $will = ($sum > 0.0) || ($pmax >= $prob_min);
        $daily[] = [
            'date' => $d,
            'precipitation_sum_mm' => $sum,
            'probability_max' => $pmax,
            'will_rain' => $will
        ];
    }

    $summary = '';
    if ($today_will_rain) {
        $summary = 'Hôm nay có khả năng mưa';
        if ($today_next_rain_time) $summary .= ' khoảng ' . $today_next_rain_time;
        $summary .= '.';
    } else {
        $summary = 'Hôm nay ít khả năng mưa.';
    }

    return [
        'ok' => true,
        'source' => 'open-meteo',
        'timezone' => $tz,
        'lat' => floatval($lat),
        'lon' => floatval($lon),
        'prob_min' => $prob_min,
        'now_vn' => date('c'),
        'now_utc' => gmdate('c'),
        'summary' => $summary,
        'today_will_rain' => $today_will_rain,
        'today_next_rain_time' => $today_next_rain_time,
        'next_rain' => $next_rain,
        'next_rain_date' => $next_rain_date,
        'next_rain_time' => $next_rain_time,
        'next_rain_in_minutes' => $next_rain_in_minutes,
        'daily' => $daily
    ];
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $raw = file_get_contents('php://input');
    $data = json_decode($raw, true);
    if (!$data || !isset($data['action']) || $data['action'] !== 'log') {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'invalid_payload']);
        exit;
    }

    $ts   = isset($data['ts']) ? $data['ts'] : date('c');
    $snap = isset($data['data']) ? $data['data'] : null;
    if ($snap === null) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'missing_data']);
        exit;
    }

    if (!file_exists($file)) {
        $arr = [];
    } else {
        $json = file_get_contents($file);
        $arr = json_decode($json, true);
        if (!is_array($arr)) {
            $arr = [];
        }
    }

    $arr[] = [
        'ts'   => $ts,
        'data' => $snap
    ];

    file_put_contents(
        $file,
        json_encode($arr, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT)
    );

    require_once __DIR__ . '/logger_firebase.php';
    logger_chay();

    echo json_encode(['ok' => true, 'count' => count($arr)]);
    exit;
}

$action = isset($_GET['action']) ? $_GET['action'] : 'history';

if ($action === 'history') {
    if (!file_exists($file)) {
        echo json_encode([]);
        exit;
    }
    $json = file_get_contents($file);
    $arr = json_decode($json, true);
    if (!is_array($arr)) {
        $arr = [];
    }
    echo json_encode($arr, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

if ($action === 'weather') {
    $lat = isset($_GET['lat']) ? trim($_GET['lat']) : '10.8231';
    $lon = isset($_GET['lon']) ? trim($_GET['lon']) : '106.6297';
    $days = isset($_GET['days']) ? intval($_GET['days']) : 7;
    $prob_min = isset($_GET['prob_min']) ? intval($_GET['prob_min']) : 50;

    $w = build_weather_open_meteo($lat, $lon, $days, $prob_min);
    if (!$w) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => 'weather_fetch_failed']);
        exit;
    }
    echo json_encode($w, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

http_response_code(400);
echo json_encode(['ok' => false, 'error' => 'unknown_action']);
