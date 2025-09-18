#!/usr/bin/env bash
# To enable encrypted delivery and make curl attacks impossible:

# 1. Set KMS_KEY_ALIAS environment variable on your Lambda/server
export KMS_KEY_ALIAS="alias/your-kms-key-name"

# 2. Ensure your Lambda role has these permissions:
# - kms:GenerateDataKey on the KMS key
# - kms:Decrypt on the KMS key (for client access)
# - s3:PutObject on encrypted-temp/* prefix

# 3. Optional: Configure STS role for client KMS access
export AWS_STS_ROLE_ARN="arn:aws:iam::123456789012:role/YourClientRole"
export AWS_STS_SESSION_NAME="w2r-client-session"

# 4. Test the security after configuration:
# ./tools/inspect_download.ps1 -Key "test_files/test.txt" -AuthToken "your-token"
# Expected: "Server returned ENCRYPTED delivery (ciphertextUrl + envelope)"