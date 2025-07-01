#include <iostream>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

int main() {
    std::cout << "Testing MySQL ODBC Connection..." << std::endl;
    
    SQLHENV henv;
    SQLHDBC hdbc;
    SQLRETURN ret;
    
    // Allocate environment handle
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::cout << "Failed to allocate environment handle" << std::endl;
        return 1;
    }
    
    // Set ODBC version
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::cout << "Failed to set ODBC version" << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        return 1;
    }
    
    // Allocate connection handle
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::cout << "Failed to allocate connection handle" << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        return 1;
    }
    
    // Connection string
    const char* connStr = "DRIVER={MySQL ODBC 9.3 ANSI Driver};SERVER=localhost;PORT=3306;UID=root;PWD=;";
    
    // Connect to database
    ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)connStr, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        std::cout << "Successfully connected to MySQL via ODBC!" << std::endl;
        
        // Test creating database
        SQLHSTMT hstmt;
        ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            ret = SQLExecDirect(hstmt, (SQLCHAR*)"CREATE DATABASE IF NOT EXISTS w2r_login", SQL_NTS);
            if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
                std::cout << "Database 'w2r_login' created/verified successfully!" << std::endl;
            } else {
                std::cout << "Failed to create database w2r_login" << std::endl;
            }
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }
        
        SQLDisconnect(hdbc);
    } else {
        std::cout << "Failed to connect to MySQL via ODBC!" << std::endl;
        
        // Get error details
        SQLCHAR sqlState[6], errorMsg[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER nativeError;
        SQLSMALLINT msgLen;
        
        ret = SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, sqlState, &nativeError, errorMsg, sizeof(errorMsg), &msgLen);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            std::cout << "Error: " << errorMsg << std::endl;
            std::cout << "SQL State: " << sqlState << std::endl;
        }
    }
    
    // Cleanup
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
    
    return 0;
}
