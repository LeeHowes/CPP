#include <functional>
#include <future>
#include <optional>
#include <iostream>

// Custom default driver for this promise
template<class PromiseT>
struct DefaultDriverImpl {
  void start() {
    const auto& shape = promise_.get_shape();
    for(int i = 0; i < shape; ++i) {
      promise_.execute_at(i);
    }
    promise_.done();
  }

  void end() {
  }

  PromiseT& promise_;
};

struct DefaultDriver {
  template<class PromiseT>
  auto operator()(PromiseT& prom){
    return DefaultDriverImpl<PromiseT>{prom};
  }
  template<class PromiseT, class ShapeF, class AtF, class DoneF>
  auto operator()(PromiseT& prom, ShapeF&&, AtF&&, DoneF&&){
    return DefaultDriverImpl<PromiseT>{prom};
  }
};


template<class PromiseT, class ShapeF, class AtF, class DoneF>
struct EndDriverImpl {
  void start() {
  }

  void end() {
    const auto& shape = shapeF_();
    for(std::decay_t<decltype(shape)> i = 0; i < shape; ++i) {
      atF_(i);
    }
    doneF_();
  }

  PromiseT& promise_;
  ShapeF shapeF_;
  AtF atF_;
  DoneF doneF_;
};

struct EndDriver {
  template<class PromiseT, class ShapeF, class AtF, class DoneF>
  auto operator()(PromiseT& prom, ShapeF&& shapeF, AtF&& atF, DoneF&& doneF){
    return EndDriverImpl<PromiseT, ShapeF, AtF, DoneF>{
      prom,
      std::forward<ShapeF>(shapeF),
      std::forward<AtF>(atF),
      std::forward<DoneF>(doneF)};
  }
};

template<class F, class OutputPromise, class Shape, class RF>
class InputPromise {
public:
  InputPromise(
      F&& f, OutputPromise&& outputPromise, int initialResult, int shape) :
      f_(std::move(f)),
      outputPromise_(std::move(outputPromise)),
      result_{initialResult},
      shape_{shape} {}

  void set_value(int value) {
    inputValue_ = std::move(value);
  }

  void set_exception(std::exception_ptr e) {
    outputException_ = std::move(e);
  }

  template<class BulkDriver = DefaultDriver>
  auto build_driver(BulkDriver driver = DefaultDriver{}) {
    return driver(*
      this,
      [this](){return this->get_shape();},
      [this](auto i){this->execute_at(i);},
      [this](){this->done();});
  }

private:
  F f_;
  OutputPromise outputPromise_;
  std::optional<int> inputValue_;
  std::optional<std::exception_ptr> outputException_;
  int result_;
  int shape_;


  friend struct DefaultDriverImpl<InputPromise>;

  const int& get_shape() const {
    return shape_;
  }

  void execute_at(int idx) { // Should be templated Shape
      if(inputValue_) {
          f_(*inputValue_, idx, result_);
      }
  }

  void done() {
    if(outputException_) {
      outputPromise_.set_exception(*std::move(outputException_));
    } else {
      outputPromise_.set_value(std::move(result_));
    }
  }
};


template<class F, class Shape, class RF>
auto bulk_then_value(
    F&& continuationFunction,
    Shape s,
    RF&& resultFactory) {
  return [continuationFunction = std::forward<F>(continuationFunction),
          resultFactory = std::forward<RF>(resultFactory),
          s = std::forward<Shape>(s)](
        auto&& outputPromise) mutable {
    using OutputPromiseRef = decltype(outputPromise);
    using OutputPromise = typename std::remove_reference<OutputPromiseRef>::type;

    return InputPromise<F, OutputPromise, Shape, RF>(
        std::move(continuationFunction),
        std::forward<OutputPromiseRef>(outputPromise),
        resultFactory(),
        s);
  };

}

struct SimpleExecutor {
template<class Continuation>
int then_execute(Continuation&& cont, int inputFuture) {
  class Promise {
  public:
      Promise(int& resultStorage, std::optional<std::exception_ptr>& exceptionStorage) : resultStorage_(resultStorage), exceptionStorage_(exceptionStorage) {}

      void set_value(int value) {
          resultStorage_ = value;
      }

      void set_exception(std::exception_ptr e) {
          exceptionStorage_ = std::move(e);
      }

  private:
      int& resultStorage_;
      std::optional<std::exception_ptr>& exceptionStorage_;
  };


  int resultStorage;
  std::optional<std::exception_ptr> exceptionStorage;
  auto boundCont = std::forward<Continuation>(cont)(
    Promise{resultStorage, exceptionStorage});

  // Use default driver for this executor
  auto driver = boundCont.build_driver();
  boundCont.set_value(inputFuture);
  driver.start();
  driver.end();
  return resultStorage;
}
};

struct BulkExecutor {
template<class Continuation>
int then_execute(Continuation&& cont, int inputFuture) {
  class Promise {
  public:
      Promise(int& resultStorage, std::optional<std::exception_ptr>& exceptionStorage) : resultStorage_(resultStorage), exceptionStorage_(exceptionStorage) {}

      void set_value(int value) {
          resultStorage_ = value;
      }

      void set_exception(std::exception_ptr e) {
          exceptionStorage_ = std::move(e);
      }

  private:
      int& resultStorage_;
      std::optional<std::exception_ptr>& exceptionStorage_;
  };


  int resultStorage;
  std::optional<std::exception_ptr> exceptionStorage;
  auto boundCont = std::forward<Continuation>(cont)(
    Promise{resultStorage, exceptionStorage});

  // This executor uses the end driver
  auto driver = boundCont.build_driver(EndDriver{});
  boundCont.set_value(inputFuture);
  driver.start();
  driver.end();
  return resultStorage;
}
};

int main() {
  {
    auto inputFuture = 2;
    auto p = bulk_then_value(
        [](const int& a, int /*idx*/, int &out){out+=a;},
        int{20}, // Shape
        []() -> int {return 0;});
    auto resultF = SimpleExecutor{}.then_execute(p, inputFuture);
    std::cout << resultF << "\n";
  }
  {
    auto inputFuture = 2;
    auto p = bulk_then_value(
        [](const int& a, int /*idx*/, int &out){out+=a;},
        int{20}, // Shape
        []() -> int {return 0;});
    auto resultF = BulkExecutor{}.then_execute(p, inputFuture);
    std::cout << resultF << "\n";
  }

  return 0;
}
