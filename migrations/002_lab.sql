CREATE TABLE lab_counter (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    n INTEGER NOT NULL DEFAULT 0
) STRICT;

INSERT INTO lab_counter (id, n) VALUES (1, 0);