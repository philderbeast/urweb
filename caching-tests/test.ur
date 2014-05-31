table foo01 : {Id : int, Bar : string} PRIMARY KEY Id
table foo10 : {Id : int, Bar : string} PRIMARY KEY Id

fun flush01 () : transaction page =
    dml (UPDATE foo01 SET Bar = "baz01" WHERE Id = 42);
    return <xml><body>
      Flushed 1!
    </body></xml>

fun flush10 () : transaction page =
    dml (UPDATE foo10 SET Bar = "baz10" WHERE Id = 42);
    return <xml><body>
      Flushed 2!
    </body></xml>

fun flush11 () : transaction page =
    dml (UPDATE foo01 SET Bar = "baz11" WHERE Id = 42);
    dml (UPDATE foo10 SET Bar = "baz11" WHERE Id = 42);
    return <xml><body>
      Flushed 1 and 2!
    </body></xml>

fun cache01 () : transaction page =
    res <- oneOrNoRows (SELECT foo01.Bar FROM foo01 WHERE foo01.Id = 42);
    return <xml><body>
      Reading 1.
      {case res of
           None => <xml></xml>
         | Some row => <xml>{[row.Foo01.Bar]}</xml>}
    </body></xml>

fun cache10 () : transaction page =
    res <- oneOrNoRows (SELECT foo10.Bar FROM foo10 WHERE foo10.Id = 42);
    return <xml><body>
      Reading 2.
      {case res of
           None => <xml></xml>
         | Some row => <xml>{[row.Foo10.Bar]}</xml>}
    </body></xml>

fun cache11 () : transaction page =
    res <- oneOrNoRows (SELECT foo01.Bar FROM foo01 WHERE foo01.Id = 42);
    bla <- oneOrNoRows (SELECT foo10.Bar FROM foo10 WHERE foo10.Id = 42);
    return <xml><body>
      Reading 1 and 2.
      {case res of
           None => <xml></xml>
         | Some row => <xml>{[row.Foo01.Bar]}</xml>}
      {case bla of
           None => <xml></xml>
         | Some row => <xml>{[row.Foo10.Bar]}</xml>}
    </body></xml>
