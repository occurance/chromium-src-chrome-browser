// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options.accounts', function() {
  const List = cr.ui.List;
  const ListItem = cr.ui.ListItem;
  const ArrayDataModel = cr.ui.ArrayDataModel;

  /**
   * Creates a new user list.
   * @param {Object=} opt_propertyBag Optional properties.
   * @constructor
   * @extends {cr.ui.List}
   */
  var UserList = cr.ui.define('list');

  UserList.prototype = {
    __proto__: List.prototype,

    pref: 'cros.accounts.users',

    /** @inheritDoc */
    decorate: function() {
      List.prototype.decorate.call(this);

      // HACK(arv): http://crbug.com/40902
      window.addEventListener('resize', cr.bind(this.redraw, this));

      this.addEventListener('click', this.handleClick_);

      var self = this;

      // Listens to pref changes.
      Preferences.getInstance().addEventListener(this.pref,
          function(event) {
            self.load_(event.value);
          });
    },

    createItem: function(user) {
      return new UserListItem(user);
    },

    /**
     * Adds given user to model and update backend.
     * @param {Object} user A user to be added to user list.
     */
    addUser: function(user) {
      this.dataModel.push(user);
      this.updateBackend_();
    },

    /**
     * Removes given user from model and update backend.
     */
    removeUser: function(user) {
      var dataModel = this.dataModel;

      var index = dataModel.indexOf(user);
      if (index >= 0) {
        dataModel.splice(index, 1);
        this.updateBackend_();
      }
    },

    /**
     * Handles the clicks on the list and triggers user removal if the click
     * is on the remove user button.
     * @private
     * @param {!Event} e The click event object.
     */
    handleClick_: function(e) {
      // Handle left button click
      if (e.button == 0) {
        var el = e.target;
        if (el.className == 'remove-user-button') {
          this.removeUser(el.parentNode.user);
        }
      }
    },

    /**
     * Loads given user list.
     * @param {Array} users An array of user object.
     */
    load_: function(users) {
      this.dataModel = new ArrayDataModel(users);
    },

    /**
     * Updates backend.
     */
    updateBackend_: function() {
      Preferences.setObjectPref(this.pref, this.dataModel.slice());
    }
  };

  /**
   * Creates a new user list item.
   * @param user The user account this represents.
   * @constructor
   * @extends {cr.ui.ListItem}
   */
  function UserListItem(user) {
    var el = cr.doc.createElement('div');
    el.user = user;
    UserListItem.decorate(el);
    return el;
  }

  /**
   * Decorates an element as a user account item.
   * @param {!HTMLElement} el The element to decorate.
   */
  UserListItem.decorate = function(el) {
    el.__proto__ = UserListItem.prototype;
    el.decorate();
  };

  UserListItem.prototype = {
    __proto__: ListItem.prototype,

    /** @inheritDoc */
    decorate: function() {
      ListItem.prototype.decorate.call(this);

      this.className = 'user-list-item';

      var icon = this.ownerDocument.createElement('img');
      icon.className = 'user-icon';
      // TODO(xiyuan): Replace this with real user picture when ready.
      icon.src = 'chrome://theme/IDR_LOGIN_DEFAULT_USER';

      var labelEmail = this.ownerDocument.createElement('span');
      labelEmail.className = 'user-email-label';
      labelEmail.textContent = this.user.email;

      var labelName = this.ownerDocument.createElement('span');
      labelName.className = 'user-name-label';
      labelName.textContent = this.user.owner ?
          localStrings.getStringF('username_format', this.user.name) :
          this.user.name;

      this.appendChild(icon);
      this.appendChild(labelEmail);
      this.appendChild(labelName);

      if (!this.user.owner) {
        var removeButton = this.ownerDocument.createElement('button');
        removeButton.className = 'remove-user-button';
        this.appendChild(removeButton);
      }
    }
  };

  return {
    UserList: UserList
  };
});
