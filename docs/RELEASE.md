# Release Guide

This document describes how to cut a release for the firmware and supporting tools.

## Checklist

- Update `CHANGELOG.md`
- Confirm CI is green
- Build locally: `pio run -e esp32dev`
- Tag the release (`vX.Y.Z`) and publish on GitHub

## Versioning

This project uses semantic versioning (`MAJOR.MINOR.PATCH`).
