# Console I/O Actors

A simple demonstration of serial console echo
is provided using 6 direct-coded assembly-language actors.

## Message-Flow Diagram
~~~
|    [poll]     [in_ready]    [do_in]     [char_in]     [do_out]    [char_out]
|      :            :            :            :            :            :
+-()-->#            :            :            :            :            :
    +-<#            :            :            :            :            :
    |  :            :            :            :            :            :
    +-(do_in,poll)-># not ready  :            :            :            :
       :            #>-+         :            :            :            :
       :            :  |         :            :            :            :
       #<--()----------+         :            :            :            :
    +-<#            :            :            :            :            :
    |  :            :            :            :            :            :
    +-(do_in,poll)-># ready      :            :            :            :
       :         +-<#            :            :            :            :
       :         |  :            :            :            :            :
       :         +----------()-->#            :            :            :
       :            :         +-<#            :            :            :
       :            :         |  :            :            :            :
       :            :         +-----(do_out)-># read       :            :
       :            :            :         +-<#            :            :
       :            :            :         |  :            :            :
       :            :            :         +-------(char)->#            :
       :            :            :            :         +-<#            :
       :            :            :            :         |  :            :
       :            :            :            :         +--(poll,char)-># write
       :            :            :            :            :            #>-+
       :            :            :            :            :            :  |
       #<--()--------------------------------------------------------------+
~~~
