# W2R Auth Server

Node.js auth gateway for the Qt client. Verifies users in DynamoDB and returns temporary AWS credentials via STS.

## Setup
1. Copy `.env.example` to `.env` and fill values.
2. Install deps: `npm i`
3. Run: `npm start`

## DynamoDB users table
- Table name: `DDB_USERS_TABLE` (default: `users`)
- Primary key: `username` (String)
- Item example:
```
{
  "username": {"S": "demo"},
  "passwordHash": {"S": "$2a$10$..."},
  "email": {"S": "demo@example.com"},
  "fullName": {"S": "Demo User"},
  "active": {"BOOL": true}
}
```

## STS role
- Create an IAM role with a trust policy allowing this server's IAM principal to assume it.
- Attach a policy minimally scoped to your S3 bucket.
- Set `AWS_STS_ROLE_ARN` in `.env`.

## API
POST /auth/login
- Body: `{ username, password }`
- Success: `{ success: true, token, user, aws: { accessKeyId, secretAccessKey, sessionToken, region, bucket, endpoint } }`
- Failure: `{ success: false, message }`

## Notes
- Only temporary credentials are returned. Client should not cache secrets long-term.
- Always run behind HTTPS in production.
