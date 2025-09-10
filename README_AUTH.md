# Auth integration notes

- The Qt client reads the auth API base URL from settings key `api/baseUrl` in the app settings (QSettings). Default is `http://localhost:3000`.
- To change it without rebuilding, launch once to generate the settings file, then edit `%AppData%/Way2RepairLoginSystem/settings.ini` and add:
```
[api]
baseUrl=http://your-server:3000
```
- The server endpoint is `POST /auth/login`.
- On success, the server returns a JWT and temporary AWS credentials (STS) which the client applies to the S3 module.
- The login UI no longer stores passwords locally even if "Remember me" is checked; only the username is stored.
