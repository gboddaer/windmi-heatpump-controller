# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability, please report it responsibly:

1. **Do NOT** create a public issue
2. Email: security@yourdomain.com (replace with your contact)
3. Or use GitHub's [private vulnerability reporting](https://github.com/yourusername/windmi-controller/security/advisories)

### What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Any mitigation steps you've found

### Response Timeline

- Acknowledgment: Within 48 hours
- Assessment: Within 5 business days
- Resolution: Within 30 days (or sooner if critical)

## Security Best Practices

This project follows these security practices:

- Input validation on all user-provided data
- Secure Modbus communication
- No hardcoded credentials
- Regular dependency updates
- Code review for security-sensitive changes

## Known Limitations

- The web interface should be used on trusted local networks only
- Modbus TCP communication uses no authentication (should be on isolated network)
- No HTTPS support (use reverse proxy with TLS if needed)
