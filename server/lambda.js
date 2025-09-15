// lambda-native.js - Native Lambda handler format
import dotenv from 'dotenv';
import jwt from 'jsonwebtoken';
import bcrypt from 'bcryptjs';
import {
  DynamoDBClient,
  GetItemCommand,
  UpdateItemCommand
} from '@aws-sdk/client-dynamodb';
import { STSClient, AssumeRoleCommand } from '@aws-sdk/client-sts';

// Load environment variables
dotenv.config({ path: '.env.auth' });
// Configure dotenv to read from .env file
dotenv.config();

// Security: Validate critical environment variables
const requiredEnvVars = ['JWT_SECRET', 'DDB_USERS_TABLE'];
const missingVars = requiredEnvVars.filter(varName => !process.env[varName]);

if (missingVars.length > 0) {
  console.error('Missing required environment variables:', missingVars);
  // Don't throw in production, but log the warning
  if (process.env.NODE_ENV !== 'production') {
    console.warn('Running with default values - THIS IS NOT SECURE FOR PRODUCTION');
  }
}

// Validate JWT secret strength
const jwtSecret = process.env.JWT_SECRET || 'devsecret';
if (jwtSecret === 'devsecret' || jwtSecret.length < 32) {
  console.error('SECURITY WARNING: JWT_SECRET is weak or using default value');
  if (process.env.NODE_ENV === 'production') {
    console.error('CRITICAL: Production deployment detected with insecure JWT secret');
  }
}

// Shared AWS clients
const region = process.env.AWS_REGION || process.env.CUSTOM_AWS_REGION;
const ddb = new DynamoDBClient({ region });
const sts = new STSClient({ region });

const TABLE = process.env.DDB_USERS_TABLE;
const ROLE_ARN = process.env.AWS_STS_ROLE_ARN || '';
const SESSION_NAME = process.env.AWS_STS_SESSION_NAME;
const DURATION = Number(process.env.AWS_STS_DURATION_SECONDS || 3600);

// Rate limiting configuration
const RATE_LIMIT_TABLE = process.env.DDB_RATE_LIMIT_TABLE || TABLE;
const MAX_LOGIN_ATTEMPTS = Number(process.env.MAX_LOGIN_ATTEMPTS || 5);
const RATE_LIMIT_WINDOW = Number(process.env.RATE_LIMIT_WINDOW_MINUTES || 15) * 60 * 1000; // Convert to milliseconds
const LOCKOUT_DURATION = Number(process.env.LOCKOUT_DURATION_MINUTES || 30) * 60 * 1000; // Convert to milliseconds

// CORS configuration
const ALLOWED_ORIGINS = process.env.ALLOWED_ORIGINS ? 
  process.env.ALLOWED_ORIGINS.split(',').map(origin => origin.trim()) : 
  ['https://w2r.com', 'https://w2s.com'];

// Audit logging configuration
const AUDIT_LOG_TABLE = process.env.DDB_AUDIT_LOG_TABLE || TABLE;

// Utility functions
function logSecurityEvent(eventType, username, clientIP, userAgent, success, details = {}, event = null) {
  // Log to CloudWatch immediately
  const logData = {
    timestamp: new Date().toISOString(),
    eventType,
    username: username || 'unknown',
    clientIP: clientIP || 'unknown',
    userAgent: userAgent || 'unknown',
    success,
    sessionId: event?.requestContext?.requestId || 'unknown',
    sourceCountry: event?.requestContext?.identity?.sourceCountry || 'unknown',
    ...details
  };
  
  console.log(`SECURITY_EVENT: ${JSON.stringify(logData)}`);
  
  // Asynchronously store in DynamoDB for permanent audit trail
  if (AUDIT_LOG_TABLE) {
    const auditRecord = {
      TableName: AUDIT_LOG_TABLE,
      Item: {
        EventId: { S: `AUDIT#${Date.now()}#${Math.random().toString(36).substr(2, 9)}` },
        Timestamp: { S: logData.timestamp },
        EventType: { S: eventType },
        Username: { S: logData.username },
        ClientIP: { S: logData.clientIP },
        UserAgent: { S: logData.userAgent },
        Success: { BOOL: success },
        SessionId: { S: logData.sessionId },
        Details: { S: JSON.stringify(details) },
        TTL: { N: Math.floor(Date.now() / 1000 + (90 * 24 * 60 * 60)).toString() } // 90 days retention
      }
    };
    
    // Fire and forget - don't block the main request
    ddb.send(new UpdateItemCommand({
      TableName: AUDIT_LOG_TABLE,
      Key: auditRecord.Item,
      UpdateExpression: 'SET #ts = :ts, #et = :et, #un = :un, #ip = :ip, #ua = :ua, #s = :s, #sid = :sid, #d = :d, #ttl = :ttl',
      ExpressionAttributeNames: {
        '#ts': 'Timestamp',
        '#et': 'EventType', 
        '#un': 'Username',
        '#ip': 'ClientIP',
        '#ua': 'UserAgent',
        '#s': 'Success',
        '#sid': 'SessionId',
        '#d': 'Details',
        '#ttl': 'TTL'
      },
      ExpressionAttributeValues: {
        ':ts': auditRecord.Item.Timestamp,
        ':et': auditRecord.Item.EventType,
        ':un': auditRecord.Item.Username,
        ':ip': auditRecord.Item.ClientIP,
        ':ua': auditRecord.Item.UserAgent,
        ':s': auditRecord.Item.Success,
        ':sid': auditRecord.Item.SessionId,
        ':d': auditRecord.Item.Details,
        ':ttl': auditRecord.Item.TTL
      }
    })).catch(err => {
      console.error('Failed to store audit log:', err.message);
    });
  }
}
function sanitizeInput(input, maxLength = 500) {
  if (typeof input !== 'string') return '';
  
  // Remove null bytes and control characters except tabs, newlines, and carriage returns
  const sanitized = input
    .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, '')
    .trim()
    .slice(0, maxLength);
  
  return sanitized;
}

function validateEmail(email) {
  if (!email || typeof email !== 'string') return false;
  
  const emailRegex = /^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;
  return emailRegex.test(email) && email.length <= 320; // RFC 5321 limit
}

function validateUsername(username) {
  if (!username || typeof username !== 'string') return false;
  
  // Allow alphanumeric, underscore, hyphen, and dot
  const usernameRegex = /^[a-zA-Z0-9._-]+$/;
  const sanitized = sanitizeInput(username, 50);
  
  return usernameRegex.test(sanitized) && 
         sanitized.length >= 3 && 
         sanitized.length <= 50 &&
         !sanitized.startsWith('.') && 
         !sanitized.endsWith('.') &&
         !sanitized.includes('..');
}

function validatePassword(password) {
  if (!password || typeof password !== 'string') return { valid: false, message: 'Password is required' };
  
  if (password.length < 8) return { valid: false, message: 'Password must be at least 8 characters long' };
  if (password.length > 128) return { valid: false, message: 'Password must be no more than 128 characters long' };
  
  // Check for at least one uppercase, one lowercase, one digit, and one special character
  const hasUpper = /[A-Z]/.test(password);
  const hasLower = /[a-z]/.test(password);
  const hasDigit = /\d/.test(password);
  const hasSpecial = /[!@#$%^&*()_+\-=\[\]{};':"\\|,.<>\/?]/.test(password);
  
  if (!hasUpper || !hasLower || !hasDigit || !hasSpecial) {
    return { 
      valid: false, 
      message: 'Password must contain at least one uppercase letter, one lowercase letter, one digit, and one special character' 
    };
  }
  
  // Check for common weak patterns
  const commonPatterns = [
    /(.)\1{3,}/, // 4 or more repeated characters
    /123456/, /abcdef/, /qwerty/, /password/, /admin/
  ];
  
  for (const pattern of commonPatterns) {
    if (pattern.test(password.toLowerCase())) {
      return { valid: false, message: 'Password contains common patterns and is not secure' };
    }
  }
  
  return { valid: true };
}
async function checkRateLimit(identifier, clientIP = null) {
  try {
    const now = Date.now();
    const rateLimitKey = `RATE_LIMIT#${identifier}`;
    const ipRateLimitKey = clientIP ? `RATE_LIMIT#IP#${clientIP}` : null;
    
    // Check both user-based and IP-based rate limiting
    const checks = [rateLimitKey];
    if (ipRateLimitKey) checks.push(ipRateLimitKey);
    
    for (const key of checks) {
      const result = await ddb.send(new GetItemCommand({
        TableName: RATE_LIMIT_TABLE,
        Key: { UserId: { S: key } }
      }));
      
      if (result.Item) {
        const attempts = Number(result.Item.Attempts?.N || 0);
        const firstAttempt = Number(result.Item.FirstAttempt?.N || 0);
        const lastAttempt = Number(result.Item.LastAttempt?.N || 0);
        
        // Check if still in lockout period
        if (attempts >= MAX_LOGIN_ATTEMPTS && (now - lastAttempt) < LOCKOUT_DURATION) {
          const remainingLockout = Math.ceil((LOCKOUT_DURATION - (now - lastAttempt)) / 60000);
          return { 
            allowed: false, 
            reason: 'RATE_LIMITED',
            remainingMinutes: remainingLockout,
            attempts,
            maxAttempts: MAX_LOGIN_ATTEMPTS
          };
        }
        
        // Reset if outside the rate limit window
        if ((now - firstAttempt) > RATE_LIMIT_WINDOW) {
          await ddb.send(new UpdateItemCommand({
            TableName: RATE_LIMIT_TABLE,
            Key: { UserId: { S: key } },
            UpdateExpression: 'SET Attempts = :zero, FirstAttempt = :now, LastAttempt = :now',
            ExpressionAttributeValues: {
              ':zero': { N: '0' },
              ':now': { N: now.toString() }
            }
          }));
        }
      }
    }
    
    return { allowed: true };
  } catch (error) {
    console.error('Rate limit check failed:', error);
    // Fail open for availability, but log the error
    return { allowed: true, error: error.message };
  }
}

async function recordLoginAttempt(identifier, clientIP = null, success = false) {
  try {
    const now = Date.now();
    const keys = [`RATE_LIMIT#${identifier}`];
    if (clientIP) keys.push(`RATE_LIMIT#IP#${clientIP}`);
    
    for (const key of keys) {
      if (success) {
        // Clear rate limit on successful login
        await ddb.send(new UpdateItemCommand({
          TableName: RATE_LIMIT_TABLE,
          Key: { UserId: { S: key } },
          UpdateExpression: 'SET Attempts = :zero, LastSuccessfulLogin = :now',
          ExpressionAttributeValues: {
            ':zero': { N: '0' },
            ':now': { N: now.toString() }
          }
        }));
      } else {
        // Increment failed attempts
        await ddb.send(new UpdateItemCommand({
          TableName: RATE_LIMIT_TABLE,
          Key: { UserId: { S: key } },
          UpdateExpression: 'ADD Attempts :one SET LastAttempt = :now, FirstAttempt = if_not_exists(FirstAttempt, :now)',
          ExpressionAttributeValues: {
            ':one': { N: '1' },
            ':now': { N: now.toString() }
          }
        }));
      }
    }
  } catch (error) {
    console.error('Failed to record login attempt:', error);
    // Don't fail the auth process due to rate limiting errors
  }
}

function parseUserItem(item, userRef = null) {
  return {
    username: item.Username?.S || item.username?.S,
    passwordHash: item.PasswordHash?.S || item.passwordHash?.S,
    passwordPlain: item.Password?.S || item.password?.S,
    email: item.Email?.S || item.email?.S || '',
    fullName: item.FullName?.S || item.fullName?.S || item.Name?.S || '',
    active: item.Active?.BOOL !== false && item.active?.BOOL !== false,
    plan: item.Plan?.S || item.plan?.S || 'Free',
    isActivated: item.IsActivated?.BOOL || item.isActivated?.BOOL || false,
    planExpiry: item.PlanExpiry?.S || item.planExpiry?.S || null,
    activatedAt: item.ActivatedAt?.S || item.activatedAt?.S || null,
    userRef: userRef || item.UserId?.S
  };
}

async function getUserByUsername(username) {
  try {
    // Validate input
    if (!username || typeof username !== 'string') {
      console.error('getUserByUsername: Invalid username parameter');
      return null;
    }

    if (!TABLE) {
      console.error('getUserByUsername: DDB_USERS_TABLE environment variable not set');
      return null;
    }

    // Step 1: Look up the username reference record
    const usernameKey = `USERNAME#${username}`;
    console.log(`Looking up username record: ${usernameKey}`);
    
    const out = await ddb.send(new GetItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: usernameKey } },
      ConsistentRead: true // Ensure we get the latest data
    }));
    
    // Check if username record exists and has UserRef
    if (!out.Item) {
      console.log(`Username record not found for: ${usernameKey}`);
      return null;
    }
    
    if (!out.Item.UserRef?.S) {
      console.error(`Username record ${usernameKey} missing UserRef field. Available fields: ${Object.keys(out.Item).join(', ')}`);
      return null;
    }
    
    // Step 2: Get the actual user record using the UserRef
    const userRef = out.Item.UserRef.S;
    console.log(`Looking up user record: ${userRef}`);
    
    const userOut = await ddb.send(new GetItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: userRef } },
      ConsistentRead: true
    }));
    
    if (!userOut.Item) {
      console.error(`User record not found for UserRef: ${userRef}`);
      return null;
    }
    
    // Validate required fields exist in user record
    const requiredFields = ['Username'];
    const missingFields = requiredFields.filter(field => 
      !userOut.Item[field]?.S && !userOut.Item[field.toLowerCase()]?.S
    );
    
    if (missingFields.length > 0) {
      console.error(`User record ${userRef} missing required fields: ${missingFields.join(', ')}`);
      console.error(`Available fields: ${Object.keys(userOut.Item).join(', ')}`);
      return null;
    }
    
    // Return parsed user with the userRef properly set
    const user = parseUserItem(userOut.Item, userRef);
    console.log(`Successfully retrieved user: ${user.username} (${userRef})`);
    return user;
    
  } catch (e) {
    // Handle specific DynamoDB errors
    if (e.name === 'ResourceNotFoundException') {
      console.error(`DynamoDB table not found: ${TABLE}`);
    } else if (e.name === 'ValidationException') {
      console.error(`DynamoDB validation error: ${e.message}`);
    } else if (e.name === 'AccessDeniedException') {
      console.error(`DynamoDB access denied: ${e.message}`);
    } else {
      console.error('Unexpected error getting user:', e);
    }
    return null;
  }
}

function verifyPassword(input, user) {
  if (user.passwordHash) {
    try { return bcrypt.compareSync(input, user.passwordHash); } catch { return false; }
  }
  if (user.passwordPlain) {
    console.warn(`User ${user.username} is using plaintext password - migrate to hash`);
    return input === user.passwordPlain;
  }
  return false;
}

function validateUserPlanAccess(user) {
  const now = new Date();
  if (user.plan === 'Free') {
    return { valid: false, message: 'Free plan users cannot access the main application. Please upgrade to Premium plan to continue.', code: 'FREE_PLAN_RESTRICTION' };
  }
  if (user.plan === 'Premium' || user.plan === 'Pro') {
    if (!user.isActivated) {
      return { valid: false, message: 'Your account is not activated. Please activate your account to access the application.', code: 'ACCOUNT_NOT_ACTIVATED' };
    }
    if (user.planExpiry) {
      const expiryDate = new Date(user.planExpiry);
      if (now > expiryDate) {
        return { valid: false, message: 'Your premium plan has expired. Please renew your subscription to continue using the application.', code: 'PLAN_EXPIRED' };
      }
    }
    return { valid: true, message: 'Access granted', planInfo: { plan: user.plan, isActivated: user.isActivated, expiresAt: user.planExpiry, activatedAt: user.activatedAt } };
  }
  return { valid: false, message: 'Invalid plan type. Please contact support.', code: 'INVALID_PLAN' };
}

async function updateLastLogin(userRef) {
  try {
    // Ensure userRef is provided and valid
    if (!userRef) {
      console.error('updateLastLogin called without userRef');
      return false;
    }
    
    if (!TABLE) {
      console.error('updateLastLogin: DDB_USERS_TABLE environment variable not set');
      return false;
    }
    
    console.log(`Updating last login for user: ${userRef}`);
    
    await ddb.send(new UpdateItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: userRef } },
      UpdateExpression: 'SET LastLogin = :now',
      ExpressionAttributeValues: { ':now': { S: new Date().toISOString() } },
      // Use condition to ensure the user record exists
      ConditionExpression: 'attribute_exists(UserId)'
    }));
    
    console.log(`Successfully updated last login for: ${userRef}`);
    return true;
    
  } catch (e) {
    if (e.name === 'ConditionalCheckFailedException') {
      console.error(`User record not found for last login update: ${userRef}`);
    } else if (e.name === 'ResourceNotFoundException') {
      console.error(`DynamoDB table not found for last login update: ${TABLE}`);
    } else if (e.name === 'ValidationException') {
      console.error(`DynamoDB validation error during last login update: ${e.message}`);
    } else if (e.name === 'AccessDeniedException') {
      console.error(`DynamoDB access denied during last login update: ${e.message}`);
    } else {
      console.error('Unexpected error updating last login:', e);
    }
    return false;
  }
}

async function issueTempCreds() {
  if (!ROLE_ARN || ROLE_ARN.trim() === '') return null;
  try {
    const out = await sts.send(new AssumeRoleCommand({ RoleArn: ROLE_ARN, RoleSessionName: SESSION_NAME, DurationSeconds: DURATION }));
    const c = out.Credentials;
    if (!c) throw new Error('STS returned no credentials');
    return { accessKeyId: c.AccessKeyId, secretAccessKey: c.SecretAccessKey, sessionToken: c.SessionToken };
  } catch (e) {
    console.error('STS AssumeRole failed:', e.message);
    return null;
  }
}

function verifyToken(token) {
  try {
    // Validate token format first
    if (!token || typeof token !== 'string') {
      console.warn('Invalid token format provided');
      return null;
    }
    
    // Check if token has proper JWT structure (header.payload.signature)
    const parts = token.split('.');
    if (parts.length !== 3) {
      console.warn('Token does not have valid JWT structure');
      return null;
    }
    
    const decoded = jwt.verify(token, process.env.JWT_SECRET || 'devsecret', {
      algorithms: ['HS256'], // Only allow HMAC SHA256
      maxAge: '1h', // Reduced from 24h to 1h for better security
      clockTolerance: 30, // 30 seconds tolerance for clock skew
      issuer: 'w2r-auth-server',
      audience: 'w2r-client'
    });
    
    // Additional token validation
    if (!decoded.sub || !decoded.userId) {
      console.warn('Token missing required claims:', { 
        hasSub: !!decoded.sub, 
        hasUserId: !!decoded.userId 
      });
      return null;
    }
    
    // Validate token structure and required claims
    const requiredClaims = ['sub', 'iat', 'exp', 'iss', 'aud'];
    const missingClaims = requiredClaims.filter(claim => decoded[claim] === undefined);
    if (missingClaims.length > 0) {
      console.warn('Token missing required claims:', missingClaims);
      return null;
    }
    
    // Check for future issued tokens (clock skew protection)
    const now = Math.floor(Date.now() / 1000);
    if (decoded.iat > now + 60) { // Allow 1 minute future time
      console.warn('Token issued in the future:', { iat: decoded.iat, now });
      return null;
    }
    
    // Additional security: Check if token is too old (beyond maxAge)
    const tokenAge = now - decoded.iat;
    if (tokenAge > 3600) { // 1 hour in seconds
      console.warn('Token too old:', tokenAge);
      return null;
    }
    
    return decoded;
  } catch (err) {
    if (err.name === 'TokenExpiredError') {
      console.warn('Token expired:', err.message);
    } else if (err.name === 'JsonWebTokenError') {
      console.warn('Invalid token:', err.message);
    } else if (err.name === 'NotBeforeError') {
      console.warn('Token not active:', err.message);
    } else {
      console.warn('Token verification failed:', err.message);
    }
    return null;
  }
}

// Response helper - HTTP API v2.0 format with secure CORS
function createResponse(statusCode, body, headers = {}, event = null) {
  // Determine appropriate CORS origin
  let corsOrigin = '*'; // Default for exe applications
  
  if (event && event.headers) {
    const requestOrigin = event.headers.origin || event.headers.Origin;
    
    // If no origin header (typical for desktop apps) or ALLOW_EXE_CLIENTS is true
    if (!requestOrigin || process.env.ALLOW_EXE_CLIENTS === 'true') {
      corsOrigin = '*'; // Allow desktop applications
    }
    // For web browsers, validate origin against allowed list
    else if (requestOrigin && ALLOWED_ORIGINS.includes(requestOrigin)) {
      corsOrigin = requestOrigin;
    } else if (requestOrigin) {
      // If origin is provided but not in allowed list, use first allowed origin
      corsOrigin = ALLOWED_ORIGINS[0] || '*';
    }
  }
  
  // Generate Content Security Policy
  const csp = [
    "default-src 'none'",
    "script-src 'none'",
    "style-src 'none'",
    "img-src 'none'",
    "connect-src 'self'",
    "font-src 'none'",
    "object-src 'none'",
    "media-src 'none'",
    "frame-src 'none'",
    "worker-src 'none'",
    "frame-ancestors 'none'",
    "form-action 'none'",
    "base-uri 'none'"
  ].join('; ');
  
  const response = {
    statusCode,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'access-control-allow-origin': corsOrigin,
      'access-control-allow-methods': 'GET, POST, PUT, DELETE, OPTIONS',
      'access-control-allow-headers': 'origin, x-requested-with, content-type, accept, authorization',
      'access-control-allow-credentials': corsOrigin !== '*' ? 'true' : 'false',
      'access-control-max-age': '86400', // 24 hours
      'cache-control': 'no-store, no-cache, must-revalidate, private',
      'pragma': 'no-cache',
      'expires': '0',
      'x-content-type-options': 'nosniff',
      'x-frame-options': 'DENY',
      'x-xss-protection': '1; mode=block',
      'strict-transport-security': 'max-age=31536000; includeSubDomains; preload',
      'content-security-policy': csp,
      'referrer-policy': 'strict-origin-when-cross-origin',
      'permissions-policy': 'accelerometer=(), camera=(), geolocation=(), gyroscope=(), magnetometer=(), microphone=(), payment=(), usb=()',
      'x-permitted-cross-domain-policies': 'none',
      'cross-origin-embedder-policy': 'require-corp',
      'cross-origin-opener-policy': 'same-origin',
      'cross-origin-resource-policy': 'cross-origin',
      ...headers
    },
    body: JSON.stringify(body, null, process.env.NODE_ENV === 'development' ? 2 : 0)
  };
  
  // Add isBase64Encoded for HTTP API v2.0 compatibility
  response.isBase64Encoded = false;
  
  return response;
}

// Main Lambda handler
export const handler = async (event) => {
  console.log('Event:', JSON.stringify(event, null, 2));
  
  try {
    // Handle both HTTP API v2.0 and REST API formats
    const httpMethod = event.requestContext?.http?.method || event.httpMethod;
    const path = event.requestContext?.http?.path || event.path || event.resource || '/';
    const headers = event.headers || {};
    const rawBody = event.body;
    
    console.log('Request details:', {
      method: httpMethod,
      path: path,
      hasBody: !!rawBody,
      eventVersion: event.version || 'unknown',
      requestContext: event.requestContext ? 'present' : 'missing'
    });
    
    // Validate required environment variables
    if (!TABLE) {
      console.error('Missing required environment variable: DDB_USERS_TABLE');
      return createResponse(500, {
        success: false,
        message: 'Authentication service configuration error',
        error: 'Missing table configuration'
      }, {}, event);
    }
    
    // Handle different path formats from API Gateway
    const actualPath = path;
    console.log('Path received:', actualPath, 'Method:', httpMethod);
    
    // Handle OPTIONS requests for CORS
    if (httpMethod === 'OPTIONS') {
      return createResponse(200, { message: 'CORS preflight' }, {}, event);
    }

    // Parse body
    let body = {};
    if (rawBody) {
      try {
        body = JSON.parse(rawBody);
      } catch (e) {
        return createResponse(400, { success: false, message: 'Invalid JSON in request body' }, {}, event);
      }
    }

    // Route handling
    if (httpMethod === 'GET' && (actualPath === '/' || actualPath === '/dev' || actualPath === '/dev/')) {
      return createResponse(200, {
        success: true,
        message: 'Auth Lambda is running!',
        path: actualPath,
        environment: {
          hasTable: !!TABLE,
          tableName: TABLE || 'NOT_SET',
          hasJwtSecret: !!(process.env.JWT_SECRET || 'devsecret'),
          hasRoleArn: !!ROLE_ARN,
          region: region || 'NOT_SET',
          nodeVersion: process.version,
          timestamp: new Date().toISOString()
        }
      }, {}, event);
    }

    // Diagnostic endpoint for debugging table schema (REMOVE IN PRODUCTION)
    if (httpMethod === 'GET' && (actualPath === '/auth/debug' || actualPath === '/dev/auth/debug' || actualPath.endsWith('/auth/debug'))) {
      // Security: Only allow in development environment
      if (process.env.NODE_ENV === 'production') {
        return createResponse(404, { 
          success: false, 
          message: 'Route not found',
          error: 'ROUTE_NOT_FOUND'
        }, {}, event);
      }
      
      if (!TABLE) {
        return createResponse(500, {
          success: false,
          message: 'DDB_USERS_TABLE not configured',
          error: 'MISSING_TABLE_CONFIG'
        }, {}, event);
      }

      try {
        // Test table access by attempting to get a non-existent record
        const testKey = 'TEST#nonexistent';
        const testResult = await ddb.send(new GetItemCommand({
          TableName: TABLE,
          Key: { UserId: { S: testKey } }
        }));

        return createResponse(200, {
          success: true,
          message: 'Table access test successful',
          tableInfo: {
            tableName: TABLE,
            region: region,
            testKeyUsed: testKey,
            testSuccessful: true,
            timestamp: new Date().toISOString()
          }
        }, {}, event);
      } catch (e) {
        return createResponse(500, {
          success: false,
          message: 'Table access test failed',
          error: {
            name: e.name,
            message: e.message,
            tableName: TABLE,
            region: region
          }
        }, {}, event);
      }
    }

    if (httpMethod === 'POST' && (actualPath === '/auth/login' || actualPath === '/dev/auth/login' || actualPath.endsWith('/auth/login'))) {
      console.log('Login attempt received:', { 
        body: rawBody ? 'present' : 'missing',
        hasUsername: !!(body?.username),
        hasPassword: !!(body?.password),
        tableConfigured: !!TABLE,
        tableName: TABLE,
        region: region
      });

      const { username, password } = body;
      if (!username || !password) {
        return createResponse(400, { 
          success: false, 
          message: 'Username and password are required',
          error: 'MISSING_CREDENTIALS'
        }, {}, event);
      }

      // Enhanced input validation and sanitization
      const sanitizedUsername = sanitizeInput(username, 50);
      if (!validateUsername(sanitizedUsername)) {
        return createResponse(400, { 
          success: false, 
          message: 'Invalid username format. Use only alphanumeric characters, dots, hyphens, and underscores.',
          error: 'INVALID_USERNAME_FORMAT'
        }, {}, event);
      }

      const passwordValidation = validatePassword(password);
      if (!passwordValidation.valid) {
        return createResponse(400, { 
          success: false, 
          message: passwordValidation.message,
          error: 'INVALID_PASSWORD_FORMAT'
        }, {}, event);
      }

      const cleanUsername = sanitizedUsername.toLowerCase();

      // Get client IP for rate limiting and audit logging
      const clientIP = event.requestContext?.http?.sourceIp || 
                      event.requestContext?.identity?.sourceIp || 
                      headers['x-forwarded-for']?.split(',')[0]?.trim() ||
                      headers['x-real-ip'] || 
                      'unknown';
      
      const userAgent = headers['user-agent'] || headers['User-Agent'] || 'unknown';

      // Log login attempt
      logSecurityEvent('LOGIN_ATTEMPT', cleanUsername, clientIP, userAgent, false, {
        requestId: event.requestContext?.requestId,
        path: actualPath
      }, event);

      // Check rate limits before proceeding
      const rateLimitCheck = await checkRateLimit(cleanUsername, clientIP);
      if (!rateLimitCheck.allowed) {
        console.log(`Rate limit exceeded for user: ${cleanUsername}, IP: ${clientIP}`);
        logSecurityEvent('LOGIN_RATE_LIMITED', cleanUsername, clientIP, userAgent, false, {
          remainingMinutes: rateLimitCheck.remainingMinutes,
          attempts: rateLimitCheck.attempts,
          maxAttempts: rateLimitCheck.maxAttempts
        }, event);
        return createResponse(429, {
          success: false,
          message: `Too many failed login attempts. Account temporarily locked for ${rateLimitCheck.remainingMinutes} minutes.`,
          error: 'RATE_LIMITED',
          retryAfterMinutes: rateLimitCheck.remainingMinutes,
          maxAttempts: rateLimitCheck.maxAttempts
        }, {
          'Retry-After': (rateLimitCheck.remainingMinutes * 60).toString()
        }, event);
      }

      // Enhanced user lookup with better error handling
      console.log(`Attempting login for username: ${cleanUsername}`);
      const user = await getUserByUsername(cleanUsername);
      
      if (!user) {
        console.log(`Login failed: User not found for username: ${cleanUsername}`);
        // Record failed attempt
        await recordLoginAttempt(cleanUsername, clientIP, false);
        logSecurityEvent('LOGIN_FAILED', cleanUsername, clientIP, userAgent, false, {
          reason: 'USER_NOT_FOUND'
        }, event);
        // Generic error message for security
        return createResponse(401, { 
          success: false, 
          message: 'Invalid credentials',
          error: 'AUTHENTICATION_FAILED'
        }, {}, event);
      }
      
      // Validate user account status
      if (user.active === false) {
        console.log(`Login failed: User account disabled for username: ${cleanUsername}`);
        await recordLoginAttempt(cleanUsername, clientIP, false);
        logSecurityEvent('LOGIN_FAILED', cleanUsername, clientIP, userAgent, false, {
          reason: 'ACCOUNT_DISABLED',
          userId: user.userRef
        }, event);
        return createResponse(401, { 
          success: false, 
          message: 'Invalid credentials',
          error: 'AUTHENTICATION_FAILED'
        }, {}, event);
      }
      
      // Validate password
      if (!verifyPassword(password, user)) {
        console.log(`Login failed: Invalid password for username: ${cleanUsername}`);
        await recordLoginAttempt(cleanUsername, clientIP, false);
        logSecurityEvent('LOGIN_FAILED', cleanUsername, clientIP, userAgent, false, {
          reason: 'INVALID_PASSWORD',
          userId: user.userRef
        }, event);
        return createResponse(401, { 
          success: false, 
          message: 'Invalid credentials',
          error: 'AUTHENTICATION_FAILED'
        }, {}, event);
      }

      // Validate user plan access
      const planValidation = validateUserPlanAccess(user);
      if (!planValidation.valid) {
        console.log(`Login failed: Plan validation failed for ${cleanUsername}: ${planValidation.code}`);
        await recordLoginAttempt(cleanUsername, clientIP, false);
        logSecurityEvent('LOGIN_FAILED', cleanUsername, clientIP, userAgent, false, {
          reason: 'PLAN_VALIDATION_FAILED',
          planCode: planValidation.code,
          plan: user.plan,
          userId: user.userRef
        }, event);
        return createResponse(403, { 
          success: false, 
          message: planValidation.message, 
          code: planValidation.code, 
          plan: user.plan, 
          isActivated: user.isActivated 
        }, {}, event);
      }

      // Record successful login attempt (clears rate limit)
      await recordLoginAttempt(cleanUsername, clientIP, true);
      
      // Log successful authentication
      logSecurityEvent('LOGIN_SUCCESS', cleanUsername, clientIP, userAgent, true, {
        userId: user.userRef,
        plan: user.plan,
        isActivated: user.isActivated
      }, event);

      // Generate secure JWT token with shorter expiry
      const now = Math.floor(Date.now() / 1000);
      const tokenPayload = { 
        sub: user.username, // Standard "subject" claim
        userId: user.userRef, // Custom claim for user reference
        iat: now, // Issued at
        exp: now + (1 * 60 * 60), // Expires in 1 hour (reduced from 2 hours)
        iss: 'w2r-auth-server', // Issuer
        aud: 'w2r-client', // Audience
        jti: `${user.userRef}-${now}-${Math.random().toString(36).substr(2, 9)}`, // Unique token ID
        loginTime: now, // Track when this login occurred
        plan: user.plan, // Include plan for authorization checks
        activated: user.isActivated // Include activation status
      };
      
      const token = jwt.sign(tokenPayload, process.env.JWT_SECRET || 'devsecret', {
        algorithm: 'HS256' // Explicitly specify algorithm
      });

      // Update last login using the proper userRef
      let lastLoginUpdated = false;
      if (user.userRef) {
        lastLoginUpdated = await updateLastLogin(user.userRef);
        if (!lastLoginUpdated) {
          console.warn(`Failed to update last login for user: ${user.userRef}`);
        }
      } else {
        console.error('User object missing userRef - cannot update last login');
      }
      
      const temp = await issueTempCreds();
      const aws = temp ? { 
        ...temp, 
        region, 
        bucket: process.env.AWS_S3_BUCKET || '', 
        endpoint: process.env.AWS_S3_ENDPOINT || '' 
      } : { 
        region, 
        bucket: process.env.AWS_S3_BUCKET || '', 
        endpoint: process.env.AWS_S3_ENDPOINT || '' 
      };

      return createResponse(200, {
        success: true,
        token,
        user: {
          username: user.username,
          fullName: user.fullName || '',
          email: user.email || '',
          plan: user.plan,
          isActivated: user.isActivated,
          planExpiry: user.planExpiry
        },
        aws,
        planInfo: planValidation.planInfo,
        expiresAt: new Date(Date.now() + 1 * 60 * 60 * 1000).toISOString() // 1 hour expiry
      }, {}, event);
    }

    if (httpMethod === 'POST' && (actualPath === '/auth/validate' || actualPath === '/dev/auth/validate' || actualPath.endsWith('/auth/validate'))) {
      const authHeader = headers['Authorization'] || headers['authorization'];
      const token = authHeader && authHeader.split(' ')[1];
      const clientIP = event.requestContext?.http?.sourceIp || 'unknown';
      const userAgent = headers['user-agent'] || headers['User-Agent'] || 'unknown';
      
      if (!token) {
        logSecurityEvent('TOKEN_VALIDATION_FAILED', 'unknown', clientIP, userAgent, false, {
          reason: 'MISSING_TOKEN'
        }, event);
        return createResponse(401, { success: false, message: 'Access token required' }, {}, event);
      }

      const decoded = verifyToken(token);
      if (!decoded) {
        logSecurityEvent('TOKEN_VALIDATION_FAILED', decoded?.sub || 'unknown', clientIP, userAgent, false, {
          reason: 'INVALID_TOKEN'
        }, event);
        return createResponse(403, { success: false, message: 'Invalid or expired token' }, {}, event);
      }

      logSecurityEvent('TOKEN_VALIDATED', decoded.sub, clientIP, userAgent, true, {
        userId: decoded.userId,
        tokenAge: Math.floor(Date.now() / 1000) - decoded.iat
      }, event);

      return createResponse(200, {
        success: true,
        user: { username: decoded.sub },
        expiresAt: new Date(decoded.exp * 1000).toISOString()
      }, {}, event);
    }

    if (httpMethod === 'POST' && (actualPath === '/auth/refresh' || actualPath === '/dev/auth/refresh' || actualPath.endsWith('/auth/refresh'))) {
      const authHeader = headers['Authorization'] || headers['authorization'];
      const token = authHeader && authHeader.split(' ')[1];
      const clientIP = event.requestContext?.http?.sourceIp || 'unknown';
      const userAgent = headers['user-agent'] || headers['User-Agent'] || 'unknown';
      
      if (!token) {
        logSecurityEvent('TOKEN_REFRESH_FAILED', 'unknown', clientIP, userAgent, false, {
          reason: 'MISSING_TOKEN'
        }, event);
        return createResponse(401, { success: false, message: 'Access token required' }, {}, event);
      }

      const decoded = verifyToken(token);
      if (!decoded) {
        logSecurityEvent('TOKEN_REFRESH_FAILED', decoded?.sub || 'unknown', clientIP, userAgent, false, {
          reason: 'INVALID_TOKEN'
        }, event);
        return createResponse(403, { success: false, message: 'Invalid or expired token' }, {}, event);
      }

      const timeToExpiry = decoded.exp - Math.floor(Date.now() / 1000);
      if (timeToExpiry > 15 * 60) { // Reduced from 30 minutes to 15 minutes
        logSecurityEvent('TOKEN_REFRESH_FAILED', decoded.sub, clientIP, userAgent, false, {
          reason: 'REFRESH_NOT_NEEDED',
          timeToExpiry
        }, event);
        return createResponse(400, { success: false, message: 'Token refresh not needed yet' }, {}, event);
      }

      const now = Math.floor(Date.now() / 1000);
      const tokenPayload = { 
        sub: decoded.sub, 
        userId: decoded.userId,
        iat: now, 
        exp: now + (1 * 60 * 60), // 1 hour expiry
        iss: 'w2r-auth-server', 
        aud: 'w2r-client',
        jti: `${decoded.userId}-${now}-${Math.random().toString(36).substr(2, 9)}`,
        loginTime: decoded.loginTime || now,
        plan: decoded.plan,
        activated: decoded.activated
      };
      const newToken = jwt.sign(tokenPayload, process.env.JWT_SECRET || 'devsecret', {
        algorithm: 'HS256'
      });
      
      logSecurityEvent('TOKEN_REFRESHED', decoded.sub, clientIP, userAgent, true, {
        userId: decoded.userId,
        oldTokenId: decoded.jti,
        newTokenId: tokenPayload.jti
      }, event);
      
      return createResponse(200, {
        success: true,
        token: newToken,
        expiresAt: new Date(Date.now() + 1 * 60 * 60 * 1000).toISOString() // 1 hour expiry
      }, {}, event);
    }

    // Route not found
    return createResponse(404, { 
      success: false, 
      message: 'Route not found',
      requestedPath: actualPath,
      method: httpMethod,
      availableRoutes: [
        'GET / - Health check',
        'GET /auth/debug - Diagnostic info',
        'POST /auth/login - User authentication',
        'POST /auth/validate - Token validation', 
        'POST /auth/refresh - Token refresh'
      ]
    }, {}, event);

  } catch (error) {
    console.error('Lambda error:', error);
    
    // Handle specific error types
    let errorMessage = 'Authentication service temporarily unavailable';
    let statusCode = 500;
    let errorCode = 'INTERNAL_ERROR';
    
    if (error.name === 'ResourceNotFoundException') {
      errorMessage = 'Service configuration error';
      errorCode = 'RESOURCE_NOT_FOUND';
    } else if (error.name === 'AccessDeniedException') {
      errorMessage = 'Service permission error';
      errorCode = 'ACCESS_DENIED';
    } else if (error.name === 'ValidationException') {
      errorMessage = 'Service validation error';
      errorCode = 'VALIDATION_ERROR';
    } else if (error.name === 'ThrottlingException') {
      errorMessage = 'Service temporarily busy';
      errorCode = 'THROTTLING';
      statusCode = 429;
    } else if (error.name === 'SyntaxError' && error.message.includes('JSON')) {
      errorMessage = 'Invalid request format';
      errorCode = 'INVALID_JSON';
      statusCode = 400;
    }
    
    return createResponse(statusCode, {
      success: false,
      message: errorMessage,
      error: errorCode,
      timestamp: new Date().toISOString(),
      // Only include stack trace in development
      ...(process.env.NODE_ENV === 'development' && { stack: error.stack })
    }, {}, event);
  }
};
