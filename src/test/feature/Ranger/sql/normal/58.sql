set session role=usertest58;
BEGIN; INSERT INTO a VALUES (1); SAVEPOINT my_savepoint; INSERT INTO a VALUES (1); RELEASE SAVEPOINT my_savepoint; COMMIT;

