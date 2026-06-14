(module
  ;; Phase 10.2 reference: a cooperative scheduler reproducing Teko's M:N green
  ;; threads on a SINGLE WASM thread (no stack-switching), with:
  ;;   - a function table of green-thread bodies (call_indirect targets),
  ;;   - a run queue in linear memory (spawn enqueues {fn_index, arg}),
  ;;   - $sched_run draining the queue run-to-completion,
  ;;   - blocking channels via yield: a receive on an empty channel calls
  ;;     $sched_run (re-entrant) to run a producer, then resumes — WITHOUT
  ;;     mid-function suspension (the consumer frame stays on the native stack).
  ;;
  ;; This is the executable proof of the design. Wiring it into the compiler
  ;; (emit each `routine` body as a separate WASM function indexed in the table)
  ;; is multi-function IL lowering — tracked separately (increment 10.2b).
  ;;
  ;; Memory map: [16]=consumer result  [64..]=run queue (8 slots × {fn,arg})
  ;;             [256..]=channel {[+0]head,[+4]tail,[+8]cap,[+12]buf[cap]}
  (memory (export "memory") 1)
  (global $rq_head (mut i32) (i32.const 0))
  (global $rq_tail (mut i32) (i32.const 0))

  (type $task (func (param i32)))            ;; green-thread entry: (arg) -> ()
  (table 4 funcref)
  (elem (i32.const 0) $producer $consumer)   ;; index 0 = producer, 1 = consumer

  ;; --- run queue ------------------------------------------------------------
  (func $spawn (param $fn i32) (param $arg i32)
    (local $slot i32)
    (local.set $slot (i32.mul (global.get $rq_tail) (i32.const 8)))
    (i32.store offset=64 (local.get $slot) (local.get $fn))
    (i32.store offset=68 (local.get $slot) (local.get $arg))
    (global.set $rq_tail (i32.add (global.get $rq_tail) (i32.const 1))))

  (func $sched_run
    (local $slot i32) (local $fn i32) (local $arg i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_s (global.get $rq_head) (global.get $rq_tail)))
        (local.set $slot (i32.mul (global.get $rq_head) (i32.const 8)))
        (local.set $fn  (i32.load offset=64 (local.get $slot)))
        (local.set $arg (i32.load offset=68 (local.get $slot)))
        (global.set $rq_head (i32.add (global.get $rq_head) (i32.const 1)))
        (call_indirect (type $task) (local.get $arg) (local.get $fn))
        (br $next))))

  ;; --- channels (ring buffer, like emit_wasm.c) -----------------------------
  (func $chan_init (param $c i32)
    (i32.store offset=0 (local.get $c) (i32.const 0))   ;; head
    (i32.store offset=4 (local.get $c) (i32.const 0))   ;; tail
    (i32.store offset=8 (local.get $c) (i32.const 8)))  ;; cap

  (func $chan_put (param $c i32) (param $v i32)
    (i32.store offset=12
      (i32.add (local.get $c)
               (i32.mul (i32.load offset=4 (local.get $c)) (i32.const 4)))
      (local.get $v))
    (i32.store offset=4 (local.get $c)
      (i32.rem_u (i32.add (i32.load offset=4 (local.get $c)) (i32.const 1))
                 (i32.load offset=8 (local.get $c)))))

  ;; blocking receive: if empty, yield to the scheduler (runs a producer), then read
  (func $chan_get (param $c i32) (result i32)
    (local $r i32)
    (if (i32.eq (i32.load offset=0 (local.get $c)) (i32.load offset=4 (local.get $c)))
        (then (call $sched_run)))
    (local.set $r
      (i32.load offset=12
        (i32.add (local.get $c)
                 (i32.mul (i32.load offset=0 (local.get $c)) (i32.const 4)))))
    (i32.store offset=0 (local.get $c)
      (i32.rem_u (i32.add (i32.load offset=0 (local.get $c)) (i32.const 1))
                 (i32.load offset=8 (local.get $c))))
    (local.get $r))

  ;; --- green-thread bodies --------------------------------------------------
  (func $producer (param $c i32)            ;; put 1,2,3,4,5
    (local $i i32)
    (local.set $i (i32.const 1))
    (block $b (loop $l
      (br_if $b (i32.gt_s (local.get $i) (i32.const 5)))
      (call $chan_put (local.get $c) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l))))

  (func $consumer (param $c i32)            ;; sum 5 values into [16]
    (local $i i32) (local $sum i32)
    (block $b (loop $l
      (br_if $b (i32.ge_s (local.get $i) (i32.const 5)))
      (local.set $sum (i32.add (local.get $sum) (call $chan_get (local.get $c))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (i32.store offset=16 (i32.const 0) (local.get $sum)))

  ;; test() -> i32 : spawn consumer then producer; consumer blocks on the empty
  ;; channel, yields to run the producer, then drains. Expected sum = 15.
  (func (export "test") (result i32)
    (call $chan_init (i32.const 256))
    (call $spawn (i32.const 1) (i32.const 256))   ;; consumer first (will block+yield)
    (call $spawn (i32.const 0) (i32.const 256))   ;; producer
    (call $sched_run)
    (i32.load offset=16 (i32.const 0))))
