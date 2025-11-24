# Development Workflow

This document outlines the processes for contributing to FluidLoom, including Git usage, code reviews, and CI/CD.

## Git Workflow

We follow a **Feature Branch** workflow.

1. **Main Branch**: `main` contains the stable, production-ready code.
2. **Feature Branches**: Create a new branch for every feature or bug fix.
   ```bash
   git checkout -b feature/my-new-feature
   # or
   git checkout -b fix/issue-description
   ```

### Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/) format:

- `feat: add new stencil operator`
- `fix: resolve Vulkan validation error`
- `docs: update setup guide`
- `refactor: improve graph traversal`
- `test: add integration test for halos`

## Code Review Process

1. **Pull Request**: Open a PR against `main` when your feature is ready.
2. **Description**: Clearly describe the changes, context, and verification steps.
3. **Review**: At least one other developer must review the code.
4. **CI Checks**: Ensure all CI checks pass (build, tests, linting).

## CI/CD Pipeline

Our CI pipeline (GitHub Actions) performs the following checks:

- **Build**: Compiles the project on macOS (Clang) and Linux (GCC).
- **Tests**: Runs unit tests and integration scripts.
- **Linting**: Checks code formatting and static analysis.

## Dependency Management

- **External Libraries**: Managed via `external/` directory (git submodules) or system packages.
- **Adding Dependencies**:
  1. Add submodule to `external/`.
  2. Update `external/CMakeLists.txt`.
  3. Update `src/CMakeLists.txt` to link the new library.
