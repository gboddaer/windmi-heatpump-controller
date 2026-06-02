# Contributing to windmi-controller

Thank you for your interest in contributing to windmi-controller! This document provides guidelines for contributing to the project.

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment for all contributors.

## How to Contribute

### Reporting Bugs

Before creating a bug report:
- Check the [existing issues](https://github.com/yourusername/windmi-controller/issues)
- Verify the bug is not already fixed in the latest version

When creating a bug report:
- Use a clear, descriptive title
- Include steps to reproduce
- Include expected vs actual behavior
- Include environment details (OS, compiler version, hardware)
- Include relevant logs or error messages

### Suggesting Enhancements

When suggesting an enhancement:
- Use a clear, descriptive title
- Explain the problem this would solve
- Describe the proposed solution
- Include any alternative solutions you've considered

### Pull Requests

1. Fork the repository
2. Create a branch for your feature or fix (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass (`make test` or `ctest`)
6. Update documentation as needed
7. Commit with clear, descriptive messages
8. Push your branch
9. Open a Pull Request

## Development Setup

```bash
# Clone with submodules
git clone --recursive https://github.com/yourusername/windmi-controller.git

# Build
mkdir build && cd build
cmake ..
make

# Run tests
ctest
```

## Coding Standards

- Follow the existing code style
- Use C++17 features where appropriate
- Add unit tests for new functionality
- Update documentation for API changes
- Keep commits focused and atomic

## Commit Messages

Use the following format:
```
<type>: <description>

[optional body]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `test`: Adding or modifying tests
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `chore`: Maintenance tasks

## Questions?

Open an issue or reach out to the maintainers.
