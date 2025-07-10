# PDF Viewer - New Key Mappings

## Overview
The PDF viewer has been updated with new key mappings for improved user experience, matching the requirements for better zooming, panning, and scrolling controls.

## New Control Scheme

### 1. Zooming
- **Mouse wheel alone** → Zooming in/out
- **No Ctrl modifier required** (simplified from previous version)
- **Cursor-based zooming** - zoom focuses on cursor position
- **Range**: 35% to 500% (matching standalone viewer)

### 2. Scrolling
- **Ctrl + Mouse wheel** → Vertical scrolling
- **Vertical scroll bar** → Visual navigation and scrolling (right side)
- **Smooth scrolling** with optimized performance at high zoom
- **Progressive rendering** for better experience

### 3. Panning
- **Right mouse button drag** → Panning (horizontal & vertical)
- **Optimized throttling** at high zoom levels for smooth performance
- **Visual feedback** with cursor change during panning
- **No context menu interference** - right-click is purely for panning

### 4. Context Menu Access
- **Ctrl + Right click** → Access context menu (zoom options, search, etc.)
- **Regular right-click** → Reserved for panning only (no menu)

### 5. Scroll Bar Navigation
- **Vertical scroll bar** → Appears on right side when document is longer than viewport
- **Click and drag** → Navigate through document
- **Page step** → Based on viewport height for natural navigation
- **Auto-hide** → Only visible when needed
- **Real-time updates** → Syncs with all other navigation methods

## Performance Optimizations

### High Zoom Performance (>2.5x zoom)
- **Event throttling**: Wheel and pan events are batched for smooth performance
- **Progressive rendering**: Background texture updates for seamless experience
- **Memory management**: Smart texture cleanup to prevent memory issues
- **Batch processing**: Multiple rapid events are combined to reduce lag

### Throttling Details
- **Wheel events**: 5ms throttle, max 3 events per batch
- **Pan events**: 8ms throttle, max 5 events per batch
- **Acceleration reset**: 150ms timeout for wheel acceleration

## Implementation Details

### Event Handling
- `wheelEvent()`: Handles both zooming (default) and scrolling (with Ctrl)
- `mousePressEvent()`: Initiates panning on right button
- `mouseMoveEvent()`: Processes panning with throttling
- `mouseReleaseEvent()`: Finalizes panning and triggers texture updates
- `onVerticalScrollBarChanged()`: Handles scroll bar navigation with texture updates

### Scroll Bar Features
- **Smart positioning**: 16px width, positioned on right edge with margin
- **Auto-hide functionality**: Only visible when document exceeds viewport height
- **Percentage-based scrolling**: 0-100% range for consistent behavior
- **Optimized updates**: Integrated with throttling system for smooth high-zoom performance
- **Texture coordination**: Triggers appropriate texture updates (immediate, progressive, or throttled)
- **Real-time synchronization**: Updates position during wheel scrolling, panning, and keyboard navigation

## UI Features

### Scroll Bar
- **Location**: Right side of the viewer
- **Behavior**: 
  - Automatically appears when document is longer than viewport
  - Shows current position as percentage
  - Click and drag for direct navigation
  - Page step scrolling with mouse clicks
  - Automatically updates during keyboard/mouse scrolling
- **Integration**: Works seamlessly with all other scrolling methods

### Context Menu
The context menu (accessed via Ctrl + Right-click) displays:
- Zoom controls with shortcuts
- Control explanations:
  - "• Mouse Wheel = Zoom"
  - "• Ctrl + Mouse Wheel = Scroll"  
  - "• Right Mouse Button = Pan"
  - "• Ctrl + Right Click = Menu"
- Search functionality

**Important**: Regular right-click no longer shows the context menu to avoid interference with panning. Use Ctrl + Right-click to access the menu.

## Removed Legacy Features
- Old wheel zoom mode toggle (Ctrl+Z)
- `toggleZoomMode()`, `setWheelZoomMode()`, `getWheelZoomMode()` functions
- Complex zoom mode switching - now uses direct controls

## Keyboard Shortcuts
- **Ctrl + =**: Zoom in
- **Ctrl + -**: Zoom out
- **Ctrl + 0**: Zoom to fit
- **Ctrl + F**: Search
- **Page Up/Down**: Navigate pages
- **Arrow keys**: Fine scrolling

## Testing Notes
- Compiled successfully with only minor warnings (initialization order, unused parameters)
- All new key mappings are functional
- Performance optimizations active at high zoom levels
- Context menu updated with correct control descriptions
- Legacy zoom mode functionality completely removed
