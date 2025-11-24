# FluidLoom Documentation

This directory contains the comprehensive documentation for the FluidLoom project. The documentation is structured as a static website that renders Markdown files client-side.

## Viewing the Documentation

To view the documentation, you need to serve the `docs/` directory using a simple HTTP server. This is required because browsers typically block Cross-Origin Resource Sharing (CORS) requests for local files (`file://` protocol), which prevents the site from fetching the Markdown files.

### Using Python (Recommended)

If you have Python 3 installed:

```bash
# Navigate to the docs directory
cd docs

# Start the server
python3 -m http.server 8000
```

Then open your browser and navigate to: [http://localhost:8000](http://localhost:8000)

### Using Node.js

If you have Node.js installed:

```bash
# Install http-server globally (one-time setup)
npm install -g http-server

# Navigate to the docs directory
cd docs

# Start the server
http-server -p 8000
```

## Directory Structure

- **index.html**: The main entry point that handles routing and rendering.
- **Developer_Guide/**: Setup, workflow, standards, and testing guides.
- **Programming_Guide/**: Core concepts, patterns, and best practices.
- **API_Reference/**: Detailed API documentation for modules and classes.
- **Architecture/**: System design, diagrams, and infrastructure.
- **Tutorials/**: Step-by-step guides for learning the system.

## Editing Documentation

To edit the documentation, simply modify the corresponding Markdown (`.md`) files in the subdirectories. The changes will be reflected immediately upon refreshing the page in your browser.

- Use **GitHub Flavored Markdown**.
- Use **Mermaid** syntax for diagrams (rendered automatically).
- Update the `index.html` sidebar navigation if you add new files.
