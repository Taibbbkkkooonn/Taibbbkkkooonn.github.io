<?php
$history_file = __DIR__ . '/history.json';

// Cấu hình Email
$mail_host       = 'smtp.gmail.com';
$mail_port       = 587;
$mail_username   = 'nguyenlyphuongtaist@gmail.com';
$mail_password   = 'lpocsmihiajpdrvr';
$mail_from       = 'nguyenlyphuongtaist@gmail.com';
$mail_from_name  = 'SmartFarm Logger';
$mail_to         = 'nguyenlyphuongtai@gmail.com';

// Nạp thư viện PHPMailer (Đảm bảo đường dẫn thư mục đúng với project của bạn)
require __DIR__ . '/PHPMailer-master/PHPMailer/src/Exception.php';
require __DIR__ . '/PHPMailer-master/PHPMailer/src/PHPMailer.php';
require __DIR__ . '/PHPMailer-master/PHPMailer/src/SMTP.php';

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

function debug_log($msg, $data = null){
    $line = date('c') . ' ' . $msg;
    if ($data !== null) {
        $line .= ' ' . json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    }
    file_put_contents(__DIR__ . '/mail_debug.log', $line . "\n", FILE_APPEND);
}

function read_json_file($path, $default){
    if (!file_exists($path)) {
        return $default;
    }
    $raw = @file_get_contents($path);
    if ($raw === false || trim($raw) === '') {
        return $default;
    }
    $data = json_decode($raw, true);
    if ($data === null) {
        return $default;
    }
    return $data;
}

function send_alert_mail($subject, $html_body){
    global $mail_host, $mail_port, $mail_username, $mail_password, $mail_from, $mail_from_name, $mail_to;
    
    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host       = $mail_host;
        $mail->SMTPAuth   = true;
        $mail->Username   = $mail_username;
        $mail->Password   = $mail_password;
        $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        $mail->Port       = $mail_port;
        $mail->CharSet    = 'UTF-8';
        $mail->Encoding   = 'base64';

        $mail->setFrom($mail_from, $mail_from_name);
        $mail->addAddress($mail_to);

        $mail->isHTML(true);
        $mail->Subject = $subject;
        $mail->Body    = $html_body;
        // Tạo nội dung thay thế (plain text) từ HTML để tránh lỗi hiển thị trên client cũ
        $mail->AltBody = strip_tags(str_replace('<br>', "\n", $html_body));

        $mail->send();
        debug_log('send_alert_mail_ok', ['subject' => $subject]);
        return true;
    } catch (Exception $e) {
        debug_log('send_alert_mail_fail', ['error' => $mail->ErrorInfo]);
        return false;
    }
}

/**
 * Hàm chính: đọc bản ghi mới nhất trong history.json, nếu có vườn vượt ngưỡng thì gửi mail.
 * Hàm này được gọi từ api.php mỗi lần ESP POST dữ liệu.
 */
function logger_chay(){
    global $history_file;

    $history_arr = read_json_file($history_file, []);
    if (empty($history_arr)) {
        debug_log('history_empty');
        return;
    }

    $last = $history_arr[count($history_arr) - 1];
    $ts = isset($last['ts']) ? $last['ts'] : date('c');
    
    if (isset($last['data']) && is_array($last['data'])) {
        $data = $last['data'];
    } else {
        $data = $last;
    }

    debug_log('last_record', ['ts' => $ts, 'data' => $data]);

    $sensors_all = isset($data['sensors']) && is_array($data['sensors']) ? $data['sensors'] : [];
    $devices_all = isset($data['devices']) && is_array($data['devices']) ? $data['devices'] : [];

    $farm_keys = [];
    if (is_array($sensors_all)) {
        $farm_keys = array_merge($farm_keys, array_keys($sensors_all));
    }
    if (is_array($devices_all)) {
        $farm_keys = array_merge($farm_keys, array_keys($devices_all));
    }
    $farm_keys = array_values(array_unique($farm_keys));
    
    if (empty($farm_keys)) {
        $farm_keys = ['A', 'B', 'C'];
    }

    debug_log('farm_keys', $farm_keys);

    $events_by_farm = [];

    foreach ($farm_keys as $node) {
        $sensors = isset($sensors_all[$node]) && is_array($sensors_all[$node]) ? $sensors_all[$node] : [];
        $devices = isset($devices_all[$node]) && is_array($devices_all[$node]) ? $devices_all[$node] : [];

        $soil = null;
        if (isset($sensors['soil_pct'])) {
            $soil = floatval($sensors['soil_pct']);
        } elseif (isset($sensors['soil'])) {
            $soil = floatval($sensors['soil']);
        }

        $temp = null;
        if (isset($sensors['air_temp_c'])) {
            $temp = floatval($sensors['air_temp_c']);
        } elseif (isset($sensors['temp'])) {
            $temp = floatval($sensors['temp']);
        } elseif (isset($sensors['t'])) {
            $temp = floatval($sensors['t']);
        }

        $farm_events = [];

        if ($soil !== null) {
            if ($soil < 20.0) {
                $farm_events[] = [
                    'type'  => 'soil_low',
                    'label' => 'Độ ẩm đất thấp (<20%)',
                    'value' => $soil
                ];
            }
            if ($soil > 90.0) {
                $farm_events[] = [
                    'type'  => 'soil_high',
                    'label' => 'Độ ẩm đất cao (>90%)',
                    'value' => $soil
                ];
            }
        }

        if ($temp !== null) {
            if ($temp < 15.0) {
                $farm_events[] = [
                    'type'  => 'temp_low',
                    'label' => 'Nhiệt độ thấp (<15°C)',
                    'value' => $temp
                ];
            }
            if ($temp > 45.0) {
                $farm_events[] = [
                    'type'  => 'temp_high',
                    'label' => 'Nhiệt độ cao (>45°C)',
                    'value' => $temp
                ];
            }
        }

        if (!empty($farm_events)) {
            $events_by_farm[$node] = [
                'events'  => $farm_events,
                'sensors' => $sensors,
                'devices' => $devices
            ];
        }

        debug_log('farm_check', [
            'node'       => $node,
            'sensors'    => $sensors,
            'soil'       => $soil,
            'temp'       => $temp,
            'events_cnt' => count($farm_events)
        ]);
    }

    if (empty($events_by_farm)) {
        debug_log('no_events_no_mail');
        return;
    }

    // 1. Định nghĩa file lưu thời gian gửi lần cuối
    $file_luu_thoi_gian = __DIR__ . '/last_sent_time.log';
    $thoi_gian_hien_tai = time();
    $thoi_gian_gui_cuoi = 0;

    // 2. Đọc thời gian cũ từ file (nếu file tồn tại)
    if (file_exists($file_luu_thoi_gian)) {
        $thoi_gian_gui_cuoi = (int)file_get_contents($file_luu_thoi_gian);
    }

    // 3. Tính khoảng cách thời gian (300 giây = 5 phút)
    if ($thoi_gian_hien_tai - $thoi_gian_gui_cuoi < 300) {
        // Chưa đủ 5 phút -> Bỏ qua, không gửi mail
        debug_log('skip_mail_spam_protection', [
            'wait_seconds' => 300 - ($thoi_gian_hien_tai - $thoi_gian_gui_cuoi)
        ]);
        return; 
    }

    // ---------------------------------------------------------
    $farm_names = array_keys($events_by_farm);
    $subject = 'Cảnh báo bất thường SmartFarm: vườn ' . implode(', ', $farm_names);
    
    // Tạo nội dung mail dạng HTML
    $body = '<h3>Cảnh báo thông số bất thường SmartFarm</h3>';
    $body .= '<p><strong>Thời gian:</strong> ' . htmlspecialchars($ts) . '</p>';

    foreach ($events_by_farm as $node => $info) {
        $sensors = $info['sensors'];
        $devices = $info['devices'];
        $events  = $info['events'];

        // Lấy thông số (để hiển thị bảng)
        $air_temp = isset($sensors['air_temp_c']) ? $sensors['air_temp_c'] : (isset($sensors['t']) ? $sensors['t'] : '--');
        $air_humi = isset($sensors['air_humi']) ? $sensors['air_humi'] : (isset($sensors['h']) ? $sensors['h'] : '--');
        $soil_pct = isset($sensors['soil_pct']) ? $sensors['soil_pct'] : (isset($sensors['soil']) ? $sensors['soil'] : '--');
        
        $mode = isset($devices['mode']) ? $devices['mode'] : '--';
        $pump = isset($devices['pump']['state']) ? $devices['pump']['state'] : '--';
        $light = isset($devices['light']['state']) ? $devices['light']['state'] : '--';

        $body .= '<hr>';
        $body .= '<h4>Vườn ' . htmlspecialchars($node) . '</h4>';
        
        // Bảng thông số
        $body .= '<table border="1" cellpadding="5" cellspacing="0" style="border-collapse:collapse;">';
        $body .= '<tr><th>Thông số</th><th>Giá trị</th></tr>';
        $body .= '<tr><td>Nhiệt độ</td><td>' . htmlspecialchars($air_temp) . ' °C</td></tr>';
        $body .= '<tr><td>Độ ẩm không khí</td><td>' . htmlspecialchars($air_humi) . ' %</td></tr>';
        $body .= '<tr><td>Độ ẩm đất</td><td>' . htmlspecialchars($soil_pct) . ' %</td></tr>';
        $body .= '<tr><td>Chế độ</td><td>' . htmlspecialchars($mode) . '</td></tr>';
        $body .= '<tr><td>Bơm</td><td>' . htmlspecialchars($pump) . '</td></tr>';
        $body .= '<tr><td>Đèn</td><td>' . htmlspecialchars($light) . '</td></tr>';
        $body .= '</table>';
        
        // Mục cảnh báo
        $body .= '<p style="color:red; font-weight:bold;">Mục cảnh báo:</p><ul>';
        foreach ($events as $ev) {
            $line = $ev['label'] . ' – hiện tại ' . $ev['value'];
            $unit = ($ev['type'] === 'soil_low' || $ev['type'] === 'soil_high') ? ' %' : ' °C';
            $body .= '<li>' . htmlspecialchars($line . $unit) . '</li>';
        }
        $body .= '</ul>';
    }

    // 4. Gửi mail và Cập nhật thời gian nếu gửi thành công
    $gui_thanh_cong = send_alert_mail($subject, $body);
    if ($gui_thanh_cong) {
        // Lưu thời gian hiện tại vào file để lần sau so sánh
        file_put_contents($file_luu_thoi_gian, $thoi_gian_hien_tai);
    }
}

// Nếu gọi trực tiếp file này trên trình duyệt:
// - ?test=1 để gửi mail test
// - không test thì chạy logger_chay() theo history.json
if (php_sapi_name() !== 'cli' && basename(__FILE__) === basename($_SERVER['SCRIPT_FILENAME'])) {
    if (isset($_GET['test']) && $_GET['test'] == '1') {
        send_alert_mail('Test Logger SmartFarm', '<h3>Mail test thành công</h3><p>Đây là mail test từ file logger_firebase.php đã sửa lỗi.</p>');
        echo "Đã gửi mail test. Kiểm tra hộp thư.";
    } else {
        logger_chay();
        echo "Đã chạy logger check.";
    }
}
?>