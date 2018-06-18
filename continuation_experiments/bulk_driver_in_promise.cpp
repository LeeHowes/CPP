#include <functional>
#include <future>
#include <optional>
#include <iostream>

template<class T>
class TrivialFuture {
public:
  TrivialFuture(T val) : val_{std::move(val)} {}

  T get() && {
    return val_;
  }

private:
  T val_;
};

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

template<class F, class OutputPromise, class Shape, class RF, class BulkDriver>
class InputPromise {
public:
  InputPromise(
      F&& f, OutputPromise&& outputPromise, int initialResult, int shape, BulkDriver&& bulk_driver) :
      f_(std::move(f)),
      outputPromise_(std::move(outputPromise)),
      result_{initialResult},
      shape_{shape},
      bulkDriver_{std::forward<BulkDriver>(bulk_driver)} {}

  void set_value(int value) {
    inputValue_ = std::move(value);
  }

  void set_exception(std::exception_ptr e) {
    outputException_ = std::move(e);
  }

  auto bulk_driver() {
    return bulkDriver_(
      *this,
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
  BulkDriver bulkDriver_;

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

    return [continuationFunction = std::forward<F>(continuationFunction),
            resultFactory = std::forward<RF>(resultFactory),
            s = std::forward<Shape>(s),
            outputPromise = std::forward<OutputPromiseRef>(outputPromise)](
          auto&& bulkDriver) mutable {
      using BulkDriverRef = decltype(bulkDriver);
      using BulkDriver = typename std::remove_reference<BulkDriverRef>::type;

      return InputPromise<F, OutputPromise, Shape, RF, BulkDriver>(
          std::move(continuationFunction),
          std::forward<OutputPromiseRef>(outputPromise),
          resultFactory(),
          s,
          std::forward<decltype(bulkDriver)>(bulkDriver));
    };
  };

}

struct SimpleExecutor {
template<class Continuation>
TrivialFuture<int> then_execute(Continuation&& cont, TrivialFuture<int> inputFuture) {
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
  // Decide where to get DefaultDriver from. Might be useful to make the
  // continuation a class and get it from there.
  auto boundCont = std::forward<Continuation>(cont)(
    Promise{resultStorage, exceptionStorage})(
    DefaultDriver{});

  // Use default driver for this executor
  auto driver = boundCont.bulk_driver();
  boundCont.set_value(std::move(inputFuture).get());
  driver.start();
  driver.end();
  return TrivialFuture<int>{resultStorage};
}
};

struct BulkExecutor {
// Twoway execution function that returns a future
// {or at least in this simple example, returns the value as a proxy for the
// future}
template<class Continuation>
TrivialFuture<int> then_execute(Continuation&& cont, TrivialFuture<int> inputFuture) {
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
    Promise{resultStorage, exceptionStorage})(EndDriver{});

  // This executor uses the end driver
  auto driver = boundCont.bulk_driver();
  boundCont.set_value(std::move(inputFuture).get());
  driver.start();
  driver.end();
  return TrivialFuture<int>{resultStorage};
}

class OutputPromise {
public:
    OutputPromise(
      int& resultStorage,
      std::optional<std::exception_ptr>& exceptionStorage)
      : resultStorage_(resultStorage), exceptionStorage_(exceptionStorage) {}

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

static OutputPromise makeOutput(
    int& resultStorage, std::optional<std::exception_ptr>& exceptionStorage) {
  return OutputPromise{resultStorage, exceptionStorage};
}

// Oneway execution that assumes that the continuation already has an output
// bound
template<class Continuation>
void deferred_execute(Continuation&& cont, TrivialFuture<int> inputFuture) {
  // Bind the EndDriver as the bulk driver of the continuation
  auto boundCont = std::forward<Continuation>(cont)(EndDriver{});
  auto driver = boundCont.bulk_driver();
  boundCont.set_value(std::move(inputFuture).get());
  driver.start();
  driver.end();
}
};

int main() {
  {
    TrivialFuture<int> inputFuture{2};
    auto p = bulk_then_value(
        [](const int& a, int /*idx*/, int &out){out+=a;},
        int{20}, // Shape
        []() -> int {return 0;});
    auto resultF = SimpleExecutor{}.then_execute(p, std::move(inputFuture));
    std::cout << std::move(resultF).get() << "\n";
  }
  {
    TrivialFuture<int> inputFuture{2};
    auto p = bulk_then_value(
        [](const int& a, int /*idx*/, int &out){out+=a;},
        int{20}, // Shape
        []() -> int {return 0;});
    auto resultF = BulkExecutor{}.then_execute(p, std::move(inputFuture));
    std::cout << std::move(resultF).get() << "\n";
  }
  {
    TrivialFuture<int> inputFuture{2};
    auto p = bulk_then_value(
        [](const int& a, int /*idx*/, int &out){out+=a;},
        int{20}, // Shape
        []() -> int {return 0;});

    // Place to put result for testing
    int resultStorage;
    std::optional<std::exception_ptr> exceptionStorage;
    auto outputPromise =
      BulkExecutor::makeOutput(resultStorage, exceptionStorage);
    // Bind the output promise into the continuation immediately
    auto boundCont = std::move(p)(
      std::move(outputPromise));
    BulkExecutor{}.deferred_execute(
      std::move(boundCont), std::move(inputFuture));
    auto resultF = TrivialFuture<int>{resultStorage};

    std::cout << std::move(resultF).get() << "\n";
  }


  return 0;
}
