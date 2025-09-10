import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';
import jwt from 'jsonwebtoken';
import bcrypt from 'bcryptjs';
import {
  DynamoDBClient,
  GetItemCommand,
  ScanCommand,
  DescribeTableCommand,
  UpdateItemCommand
} from '@aws-sdk/client-dynamodb';
import {
  STSClient,
  AssumeRoleCommand
} from '@aws-sdk/client-sts';
import rateLimit from 'express-rate-limit';
import helmet from 'helmet';

dotenv.config();

const app = express();

// Security middleware
app.use(helmet({
  contentSecurityPolicy: {
    directives: {
      defaultSrc: ["'self'"],
      styleSrc: ["'self'", "'unsafe-inline'"],
      scriptSrc: ["'self'"],
      imgSrc: ["'self'", "data:", "https:"],
    },
  },
  hsts: {
    maxAge: 31536000,
    includeSubDomains: true,
    preload: true
  }
}));

// Rate limiting for login attempts
const loginLimiter = rateLimit({
  windowMs: 15 * 60 * 1000, // 15 minutes
  max: 5, // Maximum 5 login attempts per 15 minutes per IP
  message: { 
    success: false, 
    message: 'Too many login attempts. Please try again in 15 minutes.' 
  },
  standardHeaders: true,
  legacyHeaders: false,
  skip: (req) => {
    // Skip rate limiting for local development
    const isLocal = req.ip === '127.0.0.1' || req.ip === '::1' || req.ip === '::ffff:127.0.0.1';
    return process.env.NODE_ENV === 'development' && isLocal;
  }
});

// General API rate limiting
const apiLimiter = rateLimit({
  windowMs: 15 * 60 * 1000, // 15 minutes
  max: 100, // Maximum 100 requests per 15 minutes per IP
  message: { success: false, message: 'API rate limit exceeded' }
});

app.use('/auth', loginLimiter);
app.use('/api', apiLimiter);

// CORS configuration
const corsOrigins = process.env.CORS_ORIGIN ? process.env.CORS_ORIGIN.split(',') : ['http://localhost:3000'];
app.use(cors({
  origin: corsOrigins,
  credentials: true,
  optionsSuccessStatus: 200
}));

// Body parsing with size limits
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true, limit: '10mb' }));

// AWS SDK v3 clients - explicitly use credentials from environment
const region = process.env.AWS_REGION || 'us-east-1';
const awsConfig = {
  region,
  credentials: {
    accessKeyId: process.env.AWS_ACCESS_KEY_ID,
    secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY,
    ...(process.env.AWS_SESSION_TOKEN && { sessionToken: process.env.AWS_SESSION_TOKEN })
  }
};

const ddb = new DynamoDBClient(awsConfig);
const sts = new STSClient(awsConfig);

const TABLE = process.env.DDB_USERS_TABLE || 'w2s-user-table';
console.log(`Using DynamoDB table: ${TABLE} in region: ${region}`);

const ROLE_ARN = process.env.AWS_STS_ROLE_ARN || '';
const SESSION_NAME = process.env.AWS_STS_SESSION_NAME || 'w2r-login-session';
const DURATION = Number(process.env.AWS_STS_DURATION_SECONDS || 3600);

async function getUserByUsername(username) {
  try {
    // First, get the username reference to find the UserRef
    const usernameKey = `USERNAME#${username}`;
    
    const cmd = new GetItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: usernameKey } }
    });
    const out = await ddb.send(cmd);
    
    if (!out.Item || !out.Item.UserRef) {
      return null;
    }
    
    const userRef = out.Item.UserRef.S;
    
    // Now get the actual user profile using the UserRef
    const userCmd = new GetItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: userRef } }
    });
    const userOut = await ddb.send(userCmd);
    
    if (!userOut.Item) {
      return null;
    }
    
    return parseUserItem(userOut.Item);
    
  } catch (e) {
    console.error('Error getting user:', e.message);
    return null;
  }
}

function parseUserItem(item) {
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
    activatedAt: item.ActivatedAt?.S || item.activatedAt?.S || null
  };
}

function verifyPassword(input, user) {
  // Security: Only allow bcrypt hashed passwords for better security
  if (user.passwordHash) {
    try { 
      return bcrypt.compareSync(input, user.passwordHash);
    } catch (e) {
      console.error('Password verification error:', e.message);
      return false;
    }
  }
  
  // Fallback for plain passwords (should be migrated to hashed)
  if (user.passwordPlain) {
    console.warn(`User ${user.username} is using plaintext password - consider migrating to hash`);
    return input === user.passwordPlain;
  }
  
  return false;
}

// Plan and activation validation
function validateUserPlanAccess(user) {
  const now = new Date();
  
  // Check if plan is Free
  if (user.plan === 'Free') {
    return {
      valid: false,
      message: 'Free plan users cannot access the main application. Please upgrade to Premium plan to continue.',
      code: 'FREE_PLAN_RESTRICTION'
    };
  }
  
  // For Premium users, check activation status
  if (user.plan === 'Premium' || user.plan === 'Pro') {
    // Check if account is activated
    if (!user.isActivated) {
      return {
        valid: false,
        message: 'Your account is not activated. Please activate your account to access the application.',
        code: 'ACCOUNT_NOT_ACTIVATED'
      };
    }
    
    // Check if plan has expired
    if (user.planExpiry) {
      const expiryDate = new Date(user.planExpiry);
      if (now > expiryDate) {
        return {
          valid: false,
          message: 'Your premium plan has expired. Please renew your subscription to continue using the application.',
          code: 'PLAN_EXPIRED'
        };
      }
    }
    
    // All checks passed for premium user
    return {
      valid: true,
      message: 'Access granted',
      planInfo: {
        plan: user.plan,
        isActivated: user.isActivated,
        expiresAt: user.planExpiry,
        activatedAt: user.activatedAt
      }
    };
  }
  
  // Unknown plan type
  return {
    valid: false,
    message: 'Invalid plan type. Please contact support.',
    code: 'INVALID_PLAN'
  };
}

// Password strength validation
function validatePasswordStrength(password) {
  if (!password || password.length < 8) {
    return { valid: false, message: 'Password must be at least 8 characters long' };
  }
  
  const hasUpper = /[A-Z]/.test(password);
  const hasLower = /[a-z]/.test(password);
  const hasNumber = /\d/.test(password);
  const hasSpecial = /[!@#$%^&*(),.?":{}|<>]/.test(password);
  
  if (!hasUpper || !hasLower || !hasNumber) {
    return { 
      valid: false, 
      message: 'Password must contain uppercase, lowercase, and numeric characters' 
    };
  }
  
  return { valid: true };
}

// JWT token validation middleware
function authenticateToken(req, res, next) {
  const authHeader = req.headers['authorization'];
  const token = authHeader && authHeader.split(' ')[1]; // Bearer TOKEN
  
  if (!token) {
    return res.status(401).json({ success: false, message: 'Access token required' });
  }
  
  try {
    const decoded = jwt.verify(token, process.env.JWT_SECRET || 'devsecret');
    req.user = decoded;
    next();
  } catch (e) {
    return res.status(403).json({ success: false, message: 'Invalid or expired token' });
  }
}

// Update user's last login timestamp
async function updateLastLogin(userRef) {
  try {
    await ddb.send(new UpdateItemCommand({
      TableName: TABLE,
      Key: { UserId: { S: userRef } },
      UpdateExpression: 'SET LastLogin = :now',
      ExpressionAttributeValues: {
        ':now': { S: new Date().toISOString() }
      }
    }));
  } catch (e) {
    console.error('Failed to update last login:', e.message);
  }
}

async function issueTempCreds() {
  if (!ROLE_ARN || ROLE_ARN.trim() === '') {
    // Use current credentials directly (no STS)
    return {
      accessKeyId: process.env.AWS_ACCESS_KEY_ID || '',
      secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY || '',
      sessionToken: process.env.AWS_SESSION_TOKEN || ''
    };
  }
  
  try {
    const out = await sts.send(new AssumeRoleCommand({
      RoleArn: ROLE_ARN,
      RoleSessionName: SESSION_NAME,
      DurationSeconds: DURATION
    }));
    const c = out.Credentials;
    if (!c) throw new Error('STS returned no credentials');
    return {
      accessKeyId: c.AccessKeyId,
      secretAccessKey: c.SecretAccessKey,
      sessionToken: c.SessionToken
    };
  } catch (e) {
    console.error('STS AssumeRole failed, falling back to current credentials:', e.message);
    // Fallback to current credentials if STS fails
    return {
      accessKeyId: process.env.AWS_ACCESS_KEY_ID || '',
      secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY || '',
      sessionToken: process.env.AWS_SESSION_TOKEN || ''
    };
  }
}

app.post('/auth/login', async (req, res) => {
  try {
    const { username, password } = req.body || {};
    
    // Input validation
    if (!username || !password) {
      return res.status(400).json({ 
        success: false, 
        message: 'Username and password are required' 
      });
    }

    // Sanitize username
    const cleanUsername = String(username).trim().toLowerCase();
    if (cleanUsername.length < 3 || cleanUsername.length > 50) {
      return res.status(400).json({ 
        success: false, 
        message: 'Username must be between 3 and 50 characters' 
      });
    }

    // Query DynamoDB users table 
    const u = await getUserByUsername(cleanUsername);
    if (!u) {
      // Use same error message to prevent username enumeration
      return res.status(401).json({ 
        success: false, 
        message: 'Invalid credentials' 
      });
    }

    // Check if account is active
    if (u.active === false) {
      return res.status(401).json({ 
        success: false, 
        message: 'Account is disabled. Contact support.' 
      });
    }

    // Verify password
    if (!verifyPassword(password, u)) {
      return res.status(401).json({ 
        success: false, 
        message: 'Invalid credentials' 
      });
    }

    // Validate plan access before granting login
    const planValidation = validateUserPlanAccess(u);
    if (!planValidation.valid) {
      return res.status(403).json({
        success: false,
        message: planValidation.message,
        code: planValidation.code,
        plan: u.plan,
        isActivated: u.isActivated
      });
    }

    // Create JWT with secure payload
    const tokenPayload = {
      sub: u.username,
      iat: Math.floor(Date.now() / 1000),
      exp: Math.floor(Date.now() / 1000) + (2 * 60 * 60), // 2 hours
      iss: 'w2r-auth-server',
      aud: 'w2r-client'
    };
    
    const token = jwt.sign(tokenPayload, process.env.JWT_SECRET || 'devsecret');

    // Update last login timestamp
    await updateLastLogin(u.userRef || `USER#${u.username}`);

    // Get AWS credentials
    const temp = await issueTempCreds();
    const aws = {
      ...temp,
      region,
      bucket: process.env.AWS_S3_BUCKET || '',
      endpoint: process.env.AWS_S3_ENDPOINT || ''
    };

    // Security headers for response
    res.set({
      'Cache-Control': 'no-store',
      'Pragma': 'no-cache'
    });

    res.json({
      success: true,
      token,
      user: { 
        username: u.username, 
        fullName: u.fullName || '', 
        email: u.email || '',
        plan: u.plan,
        isActivated: u.isActivated,
        planExpiry: u.planExpiry
      },
      aws,
      planInfo: planValidation.planInfo,
      expiresAt: new Date(Date.now() + 2 * 60 * 60 * 1000).toISOString()
    });
    
  } catch (e) {
    console.error('Login error:', e);
    res.status(500).json({ 
      success: false, 
      message: 'Authentication service temporarily unavailable' 
    });
  }
});

// Token validation endpoint
app.post('/auth/validate', authenticateToken, (req, res) => {
  res.json({
    success: true,
    user: { username: req.user.sub },
    expiresAt: new Date(req.user.exp * 1000).toISOString()
  });
});

// Token refresh endpoint
app.post('/auth/refresh', authenticateToken, async (req, res) => {
  try {
    // Check if token is close to expiry (within 30 minutes)
    const timeToExpiry = req.user.exp - Math.floor(Date.now() / 1000);
    if (timeToExpiry > 30 * 60) {
      return res.status(400).json({
        success: false,
        message: 'Token refresh not needed yet'
      });
    }

    // Issue new token
    const tokenPayload = {
      sub: req.user.sub,
      iat: Math.floor(Date.now() / 1000),
      exp: Math.floor(Date.now() / 1000) + (2 * 60 * 60), // 2 hours
      iss: 'w2r-auth-server',
      aud: 'w2r-client'
    };
    
    const newToken = jwt.sign(tokenPayload, process.env.JWT_SECRET || 'devsecret');

    res.json({
      success: true,
      token: newToken,
      expiresAt: new Date(Date.now() + 2 * 60 * 60 * 1000).toISOString()
    });
    
  } catch (e) {
    console.error('Token refresh error:', e);
    res.status(500).json({ 
      success: false, 
      message: 'Token refresh failed' 
    });
  }
});const port = Number(process.env.PORT || 3000);
app.listen(port, () => {
  console.log(`Auth server running on http://localhost:${port}`);
});
