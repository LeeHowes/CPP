---
title: "Global execution context"
document: PXXXXR0
date: 2021-12-30
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---


# Introduction
[@P2300R2] describes a rounded set of primitives for asynchronous and parallel execution that give a firm grounding for the future.
However, the paper lacks a standard execution context and scheduler.
As noted in [@P2079R1], the earlier `static_thread_pool` had many shortcomings and was not included in [@P2300R2] for that reason.
The global execution context proposed in [@P2079R1] was an important start, but needs updating to match [@P2300R2] and we believe to simplify the design and focus on solving a very specific problem.

This paper proposes a specific solution: a global, parallel execution context and scheduler. Lifetime management and other functionality is delegated to other papers.
