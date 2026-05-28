static void handle_login_keys(KeyEvent event) {
    if (event.code == KEY_ENTER) {
        open_desktop();
    } else if (event.code == KEY_ESC) {
        shutdown_system();
    }
}
