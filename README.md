A speedrunning-intended macro for DOOM Eternal that allows you to spam inputs without a freescroll mouse. 

# Downloading and installing

If you just want to get the macro, check the [releases tab](https://github.com/henyK/doom-eternal-macro/releases), and download the latest `DOOMEternalMacro.zip`. Extract the contents wherever you want, start the game and then run the executable.


# bindings.txt

The default bindings for this macro include the `middle mouse button` for `spam mousewheel down` & `X2 mouse button (side button)` for `spam mousewheel up`. If you don't like using these specific keys you can change them in `bindings.txt`. This file has the following format:
`<key code down> <key code up>` or
`<key code> <mousewheel direction>`

For a list of possible key codes see [here](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).

When using the second format variant, it defaults to `Down` if no direction is specified.

Here are some examples:

 - Bind `middle mouse button` to spamming `wheel down` & `X2 mouse button` to spamming `wheel up`. Use:
    - `0x04 0x06`
 - Bind `middle mouse button` to spamming `wheel down`. Use either:
    - `4` or
    - `0x04` or
    - `0x04 Down`
 - Bind `middle mouse button` to spamming `wheel up`. Use:
    - `0x04 Up` 

To activate the macro in-game, just hold down the respective key for as long as necessary. The macro will continuously spam inputs, just as a freescroll mousewheel that you would keep spinning.


# Is this allowed in speedruns?

Currently all DOOM Eternal speedrun categories (Any%, 100%, All Collectibles) on the official leaderboard (https://www.speedrun.com/doom_eternal) allow this macro to be used. This is to level the playing field as quite a few glitches strongly benefit from using a freescroll mousewheel.
