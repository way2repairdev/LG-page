<?php
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// Handle preflight OPTIONS request
if ($_SERVER['REQUEST_METHOD'] == 'OPTIONS') {
    exit(0);
}

// Define the base directory to browse (adjust this path)
$baseDir = __DIR__ . '/files'; // Create a 'files' folder in your htdocs
$webBaseDir = '/files'; // Web path

// Create base directory if it doesn't exist
if (!is_dir($baseDir)) {
    mkdir($baseDir, 0755, true);
    
    // Create some sample files for testing
    file_put_contents($baseDir . '/config.txt', "# Sample Configuration File\nserver_name=localhost\nport=8080\ndebug=true");
    file_put_contents($baseDir . '/log.txt', "[INFO] Server started\n[INFO] Connection established\n[WARN] High memory usage");
    file_put_contents($baseDir . '/data.json', '{"name": "test", "value": 123, "active": true}');
    
    // Create a subfolder
    mkdir($baseDir . '/logs', 0755, true);
    file_put_contents($baseDir . '/logs/access.log', "127.0.0.1 - GET / 200\n127.0.0.1 - GET /api/files.php 200");
    file_put_contents($baseDir . '/logs/error.log', "[ERROR] File not found: missing.txt\n[ERROR] Database connection failed");
}

function scanDirectory($dir, $webPath, $maxDepth = 3, $currentDepth = 0) {
    $result = array();
    
    if ($currentDepth >= $maxDepth || !is_dir($dir)) {
        return $result;
    }
    
    $files = scandir($dir);
    
    foreach ($files as $file) {
        if ($file === '.' || $file === '..') continue;
        
        $fullPath = $dir . '/' . $file;
        $webFullPath = $webPath . '/' . $file;
        
        if (is_dir($fullPath)) {
            // It's a directory
            $folder = array(
                'name' => $file,
                'type' => 'folder',
                'path' => $webFullPath,
                'children' => scanDirectory($fullPath, $webFullPath, $maxDepth, $currentDepth + 1)
            );
            $result[] = $folder;
        } else {
            // It's a file
            $fileInfo = array(
                'name' => $file,
                'type' => 'file',
                'path' => $webFullPath,
                'size' => filesize($fullPath),
                'modified' => date('Y-m-d H:i:s', filemtime($fullPath))
            );
            $result[] = $fileInfo;
        }
    }
    
    return $result;
}

try {
    $fileList = scanDirectory($baseDir, $webBaseDir);
    
    $response = array(
        'success' => true,
        'folders' => $fileList,
        'message' => 'File list loaded successfully'
    );
    
    echo json_encode($response, JSON_PRETTY_PRINT);
    
} catch (Exception $e) {
    $response = array(
        'success' => false,
        'error' => $e->getMessage(),
        'folders' => array()
    );
    
    http_response_code(500);
    echo json_encode($response);
}
?>
