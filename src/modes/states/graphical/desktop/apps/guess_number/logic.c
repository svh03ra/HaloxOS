static void reset_guess(void) {
    guess_input_len = 0;
    guess_input[0] = '\0';
    copy_string(guess_message, "Guess a number from 1 to 100.", sizeof(guess_message));
    guess_target = rand_range(100) + 1;
}
