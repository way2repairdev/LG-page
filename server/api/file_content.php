<?php
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// Handle preflight OPTIONS request
if ($_SERVER['REQUEST_METHOD'] == 'OPTIONS') {
    exit(0);
}

// Get the file path from POST data
$filePath = '';
if ($_SERVER['REQUEST_METHOD'] == 'POST') {
    $filePath = $_POST['file_path'] ?? '';
} else {
    $filePath = $_GET['file_path'] ?? '';
}

if (empty($filePath)) {
    $response = array(
        'success' => false,
        'error' => 'No file path provided',
        'content' => ''
    );
    http_response_code(400);
    echo json_encode($response);
    exit;
}

// Security: Remove any path traversal attempts
$filePath = str_replace(['../', '..\\', '../', '..\\'], '', $filePath);

// Convert web path to local path
$baseDir = __DIR__ . '/files';
$localPath = $baseDir . $filePath;

try {
    if (!file_exists($localPath)) {
        throw new Exception("File not found: $filePath");
    }
    
    if (!is_file($localPath)) {
        throw new Exception("Path is not a file: $filePath");
    }
    
    // Check if file is readable
    if (!is_readable($localPath)) {
        throw new Exception("File is not readable: $filePath");
    }
    
    // Get file info
    $fileSize = filesize($localPath);
    $fileType = mime_content_type($localPath);
    
    // Limit file size for safety (5MB max)
    if ($fileSize > 5 * 1024 * 1024) {
        throw new Exception("File too large (max 5MB): $filePath");
    }
    
    // Read file content
    $content = file_get_contents($localPath);
    
    if ($content === false) {
        throw new Exception("Failed to read file: $filePath");
    }
    
    // Check if it's a binary file
    if (!mb_check_encoding($content, 'UTF-8') && strpos($fileType, 'text') === false) {
        $content = "Binary file - cannot display content.\n\n";
        $content .= "File: $filePath\n";
        $content .= "Size: " . number_format($fileSize) . " bytes\n";
        $content .= "Type: $fileType\n";
        $content .= "Modified: " . date('Y-m-d H:i:s', filemtime($localPath));
    }
    
    $response = array(
        'success' => true,
        'content' => $content,
        'file_path' => $filePath,
        'size' => $fileSize,
        'type' => $fileType,
        'modified' => date('Y-m-d H:i:s', filemtime($localPath))
    );
    
    echo json_encode($response);
    
} catch (Exception $e) {
    $response = array(
        'success' => false,
        'error' => $e->getMessage(),
        'content' => '',
        'file_path' => $filePath
    );
    
    http_response_code(500);
    echo json_encode($response);
}
?>
