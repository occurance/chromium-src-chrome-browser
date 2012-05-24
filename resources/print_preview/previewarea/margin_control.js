// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('print_preview', function() {
  'use strict';

  /**
   * Draggable control for setting a page margin.
   * @param {print_preview.ticket_items.CustomMargins.Orientation} orientation
   *     Orientation of the margin control that determines where the margin
   *     textbox will be placed.
   * @constructor
   * @extends {print_preview.Component}
   */
  function MarginControl(orientation) {
    print_preview.Component.call(this);

    /**
     * Determines where the margin textbox will be placed.
     * @type {print_preview.ticket_items.CustomMargins.Orientation}
     * @private
     */
    this.orientation_ = orientation;

    /**
     * Position of the margin control in points.
     * @type {number}
     * @private
     */
    this.positionInPts_ = 0;

    /**
     * Page size of the document to print.
     * @type {!print_preview.Size}
     * @private
     */
    this.pageSize_ = new print_preview.Size(0, 0);

    /**
     * Amount to scale pixel values by to convert to pixel space.
     * @type {number}
     * @private
     */
    this.scaleTransform_ = 1;

    /**
     * Amount to translate values in pixel space.
     * @type {!print_preview.Coordinate2d}
     * @private
     */
    this.translateTransform_ = new print_preview.Coordinate2d(0, 0);

    /**
     * Position of the margin control when dragging starts.
     * @type {print_preview.Coordinate2d}
     * @private
     */
    this.marginStartPositionInPixels_ = null;

    /**
     * Position of the mouse when the dragging starts.
     * @type {print_preview.Coordinate2d}
     * @private
     */
    this.mouseStartPositionInPixels_ = null;

    /**
     * Processing timeout for the textbox.
     * @type {Object}
     * @private
     */
    this.textTimeout_ = null;

    /**
     * Value of the textbox when the timeout was started.
     * @type {?string}
     * @private
     */
    this.preTimeoutValue_ = null;

    /**
     * Textbox used to display and receive the value of the margin.
     * @type {HTMLInputElement}
     * @private
     */
    this.textbox_ = null;

    /**
     * Element of the margin control line.
     * @type {HTMLElement}
     * @private
     */
    this.marginLineEl_ = null;

    /**
     * Whether this margin control's textbox has keyboard focus.
     * @type {boolean}
     * @private
     */
    this.isFocused_ = false;

    /**
     * Whether the margin control is in an error state.
     * @type {boolean}
     * @private
     */
    this.isInError_ = false;
  };

  /**
   * Event types dispatched by the margin control.
   * @enum {string}
   */
  MarginControl.EventType = {
    // Dispatched when the margin control starts dragging.
    DRAG_START: 'print_preview.MarginControl.DRAG_START',

    // Dispatched when the text in the margin control's textbox changes.
    TEXT_CHANGE: 'print_preview.MarginControl.TEXT_CHANGE'
  };

  /**
   * CSS classes used by this component.
   * @enum {string}
   * @private
   */
  MarginControl.Classes_ = {
    TOP: 'margin-control-top',
    RIGHT: 'margin-control-right',
    BOTTOM: 'margin-control-bottom',
    LEFT: 'margin-control-left',
    TEXTBOX: 'margin-control-textbox',
    INVALID: 'invalid',
    INVISIBLE: 'invisible',
    DISABLED: 'margin-control-disabled',
    DRAGGING: 'margin-control-dragging',
    LINE: 'margin-control-line'
  };

  /**
   * Map from orientation to CSS class name.
   * @type {object.<
   *     print_preview.ticket_items.CustomMargins.Orientation,
   *     MarginControl.Classes_>}
   * @private
   */
  MarginControl.OrientationToClass_ = {};
  MarginControl.OrientationToClass_[
      print_preview.ticket_items.CustomMargins.Orientation.TOP] =
      MarginControl.Classes_.TOP;
  MarginControl.OrientationToClass_[
      print_preview.ticket_items.CustomMargins.Orientation.RIGHT] =
      MarginControl.Classes_.RIGHT;
  MarginControl.OrientationToClass_[
      print_preview.ticket_items.CustomMargins.Orientation.BOTTOM] =
      MarginControl.Classes_.BOTTOM;
  MarginControl.OrientationToClass_[
      print_preview.ticket_items.CustomMargins.Orientation.LEFT] =
      MarginControl.Classes_.LEFT;

  /**
   * Radius of the margin control in pixels. Padding of control + 1 for border.
   * @type {number}
   * @const
   * @private
   */
  MarginControl.RADIUS_ = 9;

  /**
   * Timeout after a text change after which the text in the textbox is saved to
   * the print ticket. Value in milliseconds.
   * @type {number}
   * @const
   * @private
   */
  MarginControl.TEXTBOX_TIMEOUT_ = 1000;

  MarginControl.prototype = {
    __proto__: print_preview.Component.prototype,

    /** @return {boolean} Whether this margin control is in focus. */
    getIsFocused: function() {
      return this.isFocused_;
    },

    /**
     * @return {print_preview.ticket_items.CustomMargins.Orientation}
     *     Orientation of the margin control.
     */
    getOrientation: function() {
      return this.orientation_;
    },

    /**
     * @param {number} scaleTransform New scale transform of the margin control.
     */
    setScaleTransform: function(scaleTransform) {
      this.scaleTransform_ = scaleTransform;
      // Reset position
      this.setPositionInPts(this.positionInPts_);
    },

    /**
     * @param {!print_preview.Coordinate2d} translateTransform New translate
     *     transform of the margin control.
     */
    setTranslateTransform: function(translateTransform) {
      this.translateTransform_ = translateTransform;
      // Reset position
      this.setPositionInPts(this.positionInPts_);
    },

    /**
     * @param {!print_preview.Size} pageSize New size of the document's pages.
     */
    setPageSize: function(pageSize) {
      this.pageSize_ = pageSize;
      this.setPositionInPts(this.positionInPts_);
    },

    /** @param {boolean} isVisible Whether the margin control is visible. */
    setIsVisible: function(isVisible) {
      if (isVisible) {
        this.getElement().classList.remove(MarginControl.Classes_.INVISIBLE);
      } else {
        this.getElement().classList.add(MarginControl.Classes_.INVISIBLE);
      }
    },

    /** @return {boolean} Whether the margin control is in an error state. */
    getIsInError: function() {
      return this.isInError_;
    },

    /**
     * @param {boolean} isInError Whether the margin control is in an error
     *     state.
     */
    setIsInError: function(isInError) {
      this.isInError_ = isInError;
      if (isInError) {
        this.textbox_.classList.add(MarginControl.Classes_.INVALID);
      } else {
        this.textbox_.classList.remove(MarginControl.Classes_.INVALID);
      }
    },

    /** @param {boolean} isEnabled Whether to enable the margin control. */
    setIsEnabled: function(isEnabled) {
      this.textbox_.disabled = !isEnabled;
      if (isEnabled) {
        this.getElement().classList.remove(MarginControl.Classes_.DISABLED);
      } else {
        this.getElement().classList.add(MarginControl.Classes_.DISABLED);
      }
    },

    /** @return {number} Current position of the margin control in points. */
    getPositionInPts: function() {
      return this.positionInPts_;
    },

    /**
     * @param {number} posInPts New position of the margin control in points.
     */
    setPositionInPts: function(posInPts) {
      this.positionInPts_ = posInPts;
      var orientationEnum =
          print_preview.ticket_items.CustomMargins.Orientation;
      var x = this.translateTransform_.x;
      var y = this.translateTransform_.y;
      var width = null, height = null;
      if (this.orientation_ == orientationEnum.TOP) {
        y = this.scaleTransform_ * posInPts + this.translateTransform_.y -
            MarginControl.RADIUS_;
        width = this.scaleTransform_ * this.pageSize_.width;
      } else if (this.orientation_ == orientationEnum.RIGHT) {
        x = this.scaleTransform_ * (this.pageSize_.width - posInPts) +
            this.translateTransform_.x - MarginControl.RADIUS_;
        height = this.scaleTransform_ * this.pageSize_.height;
      } else if (this.orientation_ == orientationEnum.BOTTOM) {
        y = this.scaleTransform_ * (this.pageSize_.height - posInPts) +
            this.translateTransform_.y - MarginControl.RADIUS_;
        width = this.scaleTransform_ * this.pageSize_.width;
      } else {
        x = this.scaleTransform_ * posInPts + this.translateTransform_.x -
            MarginControl.RADIUS_;
        height = this.scaleTransform_ * this.pageSize_.height;
      }
      this.getElement().style.left = Math.round(x) + 'px';
      this.getElement().style.top = Math.round(y) + 'px';
      if (width != null) {
        this.getElement().style.width = Math.round(width) + 'px';
      }
      if (height != null) {
        this.getElement().style.height = Math.round(height) + 'px';
      }
    },

    /** @return {string} The value in the margin control's textbox. */
    getTextboxValue: function() {
      return this.textbox_.value;
    },

    /** @param {string} value New value of the margin control's textbox. */
    setTextboxValue: function(value) {
      if (this.textbox_.value != value) {
        this.textbox_.value = value;
      }
    },

    /**
     * Converts a value in pixels to points.
     * @param {number} Pixel value to convert.
     * @return {number} Given value expressed in points.
     */
    convertPixelsToPts: function(pixels) {
      var pts;
      var orientationEnum =
          print_preview.ticket_items.CustomMargins.Orientation;
      if (this.orientation_ == orientationEnum.TOP) {
        pts = pixels - this.translateTransform_.y + MarginControl.RADIUS_;
        pts /= this.scaleTransform_;
      } else if (this.orientation_ == orientationEnum.RIGHT) {
        pts = pixels - this.translateTransform_.x + MarginControl.RADIUS_;
        pts /= this.scaleTransform_;
        pts = this.pageSize_.width - pts;
      } else if (this.orientation_ == orientationEnum.BOTTOM) {
        pts = pixels - this.translateTransform_.y + MarginControl.RADIUS_;
        pts /= this.scaleTransform_;
        pts = this.pageSize_.height - pts;
      } else {
        pts = pixels - this.translateTransform_.x + MarginControl.RADIUS_;
        pts /= this.scaleTransform_;
      }
      return pts;
    },

    /**
     * Translates the position of the margin control relative to the mouse
     * position in pixels.
     * @param {!print_preview.Coordinate2d} mousePosition New position of
     *     the mouse.
     * @return {!print_preview.Coordinate2d} New position of the margin control.
     */
    translateMouseToPositionInPixels: function(mousePosition) {
      return new print_preview.Coordinate2d(
          mousePosition.x - this.mouseStartPositionInPixels_.x +
              this.marginStartPositionInPixels_.x,
          mousePosition.y - this.mouseStartPositionInPixels_.y +
              this.marginStartPositionInPixels_.y);
    },

    /** @override */
    createDom: function() {
      this.setElementInternal(this.cloneTemplateInternal(
          'margin-control-template'));
      this.getElement().classList.add(MarginControl.OrientationToClass_[
          this.orientation_]);
      this.textbox_ = this.getElement().getElementsByClassName(
          MarginControl.Classes_.TEXTBOX)[0];
      this.marginLineEl_ = this.getElement().getElementsByClassName(
          MarginControl.Classes_.LINE)[0];
    },

    /** @override */
    enterDocument: function() {
      print_preview.Component.prototype.enterDocument.call(this);
      this.tracker.add(
          this.getElement(), 'mousedown', this.onMouseDown_.bind(this));
      this.tracker.add(
          this.textbox_, 'keydown', this.onTextboxKeyDown_.bind(this));
      this.tracker.add(
          this.textbox_, 'focus', this.setIsFocused_.bind(this, true));
      this.tracker.add(this.textbox_, 'blur', this.onTexboxBlur_.bind(this));
    },

    /** @override */
    exitDocument: function() {
      print_preview.Component.prototype.exitDocument.call(this);
      this.textbox_ = null;
      this.marginLineEl_ = null;
    },

    /**
     * @param {boolean} isFocused Whether the margin control is in focus.
     * @private
     */
    setIsFocused_: function(isFocused) {
      this.isFocused_ = isFocused;
    },

    /**
     * Called whenever a mousedown event occurs on the component.
     * @param {MouseEvent} event The event that occured.
     * @private
     */
    onMouseDown_: function(event) {
      if (!this.textbox_.disabled &&
          event.button == 0 &&
          (event.target == this.getElement() ||
              event.target == this.marginLineEl_)) {
        this.mouseStartPositionInPixels_ =
            new print_preview.Coordinate2d(event.x, event.y);
        this.marginStartPositionInPixels_ = new print_preview.Coordinate2d(
            this.getElement().offsetLeft, this.getElement().offsetTop);
        this.setIsInError(false);
        cr.dispatchSimpleEvent(this, MarginControl.EventType.DRAG_START);
      }
    },

    /**
     * Called when a key down event occurs on the textbox. Dispatches a
     * TEXT_CHANGE event if the "Enter" key was pressed.
     * @param {Event} event Contains the key that was pressed.
     * @private
     */
    onTextboxKeyDown_: function(event) {
      if (this.textTimeout_) {
        clearTimeout(this.textTimeout_);
        this.textTimeout_ = null;
      }
      if (event.keyIdentifier == 'Enter') {
        this.preTimeoutValue_ = null;
        cr.dispatchSimpleEvent(this, MarginControl.EventType.TEXT_CHANGE);
      } else {
        if (this.preTimeoutValue_ == null) {
          this.preTimeoutValue_ = this.textbox_.value;
        }
        this.textTimeout_ = setTimeout(
            this.onTextboxTimeout_.bind(this), MarginControl.TEXTBOX_TIMEOUT_);
      }
    },

    /**
     * Called after a timeout after the text in the textbox has changed. Saves
     * the textbox's value to the print ticket.
     * @private
     */
    onTextboxTimeout_: function() {
      this.textTimeout_ = null;
      if (this.textbox_.value != this.preTimeoutValue_) {
        cr.dispatchSimpleEvent(this, MarginControl.EventType.TEXT_CHANGE);
      }
      this.preTimeoutValue_ = null;
    },

    /**
     * Called when the textbox loses focus. Dispatches a TEXT_CHANGE event.
     */
    onTexboxBlur_: function() {
      if (this.textTimeout_) {
        clearTimeout(this.textTimeout_);
        this.textTimeout_ = null;
        this.preTimeoutValue_ = null;
      }
      this.setIsFocused_(false);
      cr.dispatchSimpleEvent(this, MarginControl.EventType.TEXT_CHANGE);
    }
  };

  // Export
  return {
    MarginControl: MarginControl
  };
});
