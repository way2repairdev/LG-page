// Sample JavaScript Configuration File
// This file contains configuration settings for the Way2Repair web interface

const config = {
    // API Configuration
    api: {
        baseUrl: 'http://localhost:8080/api',
        timeout: 10000,
        retryAttempts: 3
    },
    
    // UI Configuration
    ui: {
        theme: 'light',
        language: 'en',
        autoRefresh: true,
        refreshInterval: 30000
    },
    
    // File Browser Configuration
    fileBrowser: {
        allowedExtensions: ['.txt', '.log', '.json', '.xml', '.csv'],
        maxFileSize: 10485760, // 10MB
        previewEnabled: true
    },
    
    // Debug Configuration
    debug: {
        enabled: false,
        logLevel: 'info',
        showConsole: false
    }
};

// Export configuration
if (typeof module !== 'undefined' && module.exports) {
    module.exports = config;
}

// Global configuration object
window.W2R_CONFIG = config;
